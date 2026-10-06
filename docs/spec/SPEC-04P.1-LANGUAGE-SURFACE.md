# SPEC-04P.1: Language surface

SPEC-04P.1, edition 1 of the language surface specification, cint v0.1.0, 2026-10-06.

Status: Draft. This edition describes the language profile `cint-core-1`, which is not yet frozen: a Proposed rule may change, and an Open question may be decided, in a later edition. The text of this edition does not change after its release. Corrections that change no meaning are listed in [ERRATA.md](ERRATA.md); a change of meaning is published as a new edition. Clause identifiers, section numbers, and open-question identifiers are the same in every edition and are never reused. SPEC-02, SPEC-03, and SPEC-05 to SPEC-09 have no public edition yet; their clauses are cited by identifier and are not part of this edition. What a given release of cint implements is stated in that release's notes, not here.

## Summary

This document defines the source-level surface of `cint-core-1`: how programs are spelled, which declarations and statements exist, how values are printed, how modules and tests are organized, what the compiler reports when something is wrong, and what the language deliberately leaves out. The surface is C-family, with the statement and declaration forms of C17 [1], so a C programmer is expected to recognize it. It borrows a small number of Python conventions so that, by design intent, a Python programmer can write a correct program without learning a second idiom: digit separators [2], half-open ranges (`for i in 0..n`, after Python's `range`) [3], interpolated printing [4], and inline tests. Neither aim has yet been tested with users (section 2). Top-level statements run immediately, as they do in Terry A. Davis's HolyC [5] and in a Jupyter notebook cell [6]. There is no postfix grammar and no parenthesis-free call syntax.

The companion specifications own the meaning of arithmetic, rounding, reductions, views, kernels, ternary encodings, faults, and the host boundary: SPEC-01 (integer machine), SPEC-02 (kernels and views), SPEC-03 (memory and host boundary), SPEC-05 (ternary), SPEC-06 (workbench and tooling), and SPEC-07 (security model). This document owns the spelling of every construct, including built-in operation names and argument order (section 6.8). Where a companion section spells a construct differently, this document records the decision and files a correction (section 21.2). Every normative section carries a status label: Specified (normative for this draft), Proposed (intended; may change after prototyping), or Open (undecided; listed in section 21). Sections 2, 21, and 22 explain, list, or exclude; they state no requirements and carry no label.

## 1. Conventions

**LS-1.** **Specified.**

| Term | Meaning |
|---|---|
| must, must not | A requirement on programs or implementations. A program that violates a "must" on programs is rejected at compile time unless the text says a fault is raised at run time. |
| may | Permission. |
| compile error | A diagnostic with a `C` code (section 17). No program, library, or generated source is produced, and an existing one is left unchanged. |
| fault | A run-time stop with an `E_` name (section 17.3). Faults are not exceptions and cannot be caught, inspected, or resumed by program code, including test code (SPEC-01 IM-118). |
| literal value | The exact value (an integer in Z, that is, a mathematical integer of unbounded size, or a rational for a fraction literal) that a literal denotes before its context gives it a type (section 4.2). |
| view | A typed, shaped window on a buffer with identity, origin, strides, permissions, and placement (SPEC-02 V-1). Views are second-class: they can be passed and used locally but cannot be stored (SPEC-03 M-21, section 5.1). |
| semantic input | A value that is part of an execution's identity (SPEC-01 IM-159): input state, recorded effects, the fuel budget, and the call-depth and frame-storage bounds of section 6.7. |
| Specified | Normative for `cint-core-1` in this draft. A change requires a recorded decision. |
| Proposed | Intended design, not yet normative. |
| Open | Undecided. Each Open item appears in section 21. |

**LS-2.** A section label applies to every statement in that section unless a statement carries its own label. Examples are in `.ci` syntax and are normative where the section is Specified. Output shown after `// prints:` is exact, byte for byte, with `\n` written as a line break. Each Specified or Proposed rule carries a clause identifier `LS-<n>`, written in bold at its start; its label is the one written on it or, when it carries none, the one on its section. Examples and rationale that follow a rule belong to that rule. The identifiers LS-1 to LS-311 were assigned in document order. An identifier is never reused or renumbered: a rule added later takes the next unused number wherever it appears, and a withdrawn rule keeps its number with the word "Withdrawn." Conformance cases, `.expect` files (SPEC-09 9.2), and other sections cite a single rule by its identifier, for example SPEC-04 LS-25; a citation by section number cites every rule in that section. Open items carry no identifier.

## 2. Design goals and reader mapping

This section is explanatory and carries no label. The goals state aims. They are not conformance requirements; the normative content is in the sections they cite.

1. Aim: a Python or C developer can read a `cint-core-1` program without a reference manual and can write one after the tutorial in section 19. This is a design aim. No user study has yet tested it.
2. Every operator states its overflow intent. Nothing converts implicitly between runtime numeric types (sections 6.6, 7.5).
3. Within the defined scope of section 16, every compiled program has exactly one meaning for given semantic inputs on every conforming implementation. Section 16 lists what the scope excludes.
4. Diagnostics carry exact operands and a source position, and propose explicit fixes that never silence a fault (section 17).
5. The surface stays small: one way to declare a variable, two loop forms, and one print mechanism.

The Python spellings below are those of the Python Language Reference [4] and the Python Standard Library [3]. The C spellings are those of ISO/IEC 9899:2018 [1], except for the GCC case-range extension [7].

| Python habit | `cint-core-1` spelling | Note |
|---|---|---|
| `for i in range(n):` | `for i in 0..n { }` | Half-open, as in Python. |
| `for i, x in enumerate(a):` | `for i, x in a { }` | |
| `1_000_000` | `1_000_000` | Same as PEP 515 [2], except that `_` may not follow a base prefix (section 3.5). |
| `f"x={x}"` / `print(...)` | `"x={x}\n";` | A bare string statement prints. No automatic newline. |
| `f"{x=}"` | `"{x=}\n";` | Same debug form, added in Python 3.8 [8]. |
| `a // b`, `a % b` | `a / b`, `a % b` | Floor division, as in Python (SPEC-01 IM-35). |
| `0 <= i < n` | `0 <= i && i < n` | Chained comparisons are rejected (C2102, section 7.3). |
| `a[-1]` | `a[len(a) - 1]` | Negative indices are not supported; `a[-1]` faults with `E_BOUNDS`. |
| `try: ... except E:` | `f() catch (e) { ... }` | For expected errors only. Faults are not catchable. |
| `with x:` / cleanup | `defer cleanup(x);` | Runs when the block exits normally (LS-185). It does not run after a fault (LS-188): the runtime releases the host resources the program opened, and the host program releases what it acquired around the entry (SPEC-03 A-7b). |
| `assert x == y` and pytest | `assert(x == y);` inside `test "name" { }` | Both operands are reported on failure, as pytest reports them [9]. |
| `float` | none | Use fixed point (`Q32.32`) or scaled integers. |

| C habit | `cint-core-1` spelling | Difference |
|---|---|---|
| `int64_t x = 0;` | `I64 x = 0;` | |
| `(int32_t)x` | `x as I32` | Checked; faults with `E_NARROW` if the value does not fit. `x as% I32` wraps. |
| `x + y` on signed overflow | `x + y` | Faults with `E_OVERFLOW`. Never undefined. |
| `switch` with fallthrough | `switch` without fallthrough | `fallthrough;` must be written. Empty case bodies are rejected (C4023). |
| `case 1 ... 5:` (GCC) | `case 1..=5:` | Case ranges are written inclusive; `case 1..5:` is C4022. |
| `#include`, `#define` | `import name;`, `const` | No preprocessor. |
| `printf("%d\n", x)` | `"{x}\n";` | |
| `if (x) ...` with integer `x` | `if (x != 0) { ... }` | Conditions must be `Bool`. Braces are mandatory. |
| `a & b == c` | `(a & b) == c` | Bitwise operators bind tighter than comparisons (section 7.1). |
| `int *p; p + 1` | views and indices | No pointers in source. |

## 3. Source text and lexical structure

### 3.1 Files, encoding, and language profile

**Specified.**

- **LS-3.** A source file has the extension `.ci` and is one module (section 11). Files with the extension `.cint` belong to the legacy profile `cint-bt27-legacy` and are rejected by a `cint-core-1` compiler with diagnostic C1001.
- **LS-4.** Source text is UTF-8 [10] without a byte-order mark. A byte-order mark, an invalid UTF-8 sequence, or the byte 0x00 anywhere in the file is a compile error (C1002). A byte-order mark is U+FEFF at offset 0; U+FEFF anywhere else is a `Default_Ignorable_Code_Point` and is C1003 (LS-6).
- **LS-5.** Outside string literals, byte-string literals, character literals, and comments, only ASCII characters may appear.
- **LS-6.** The following must not appear unescaped anywhere, including comments and strings (C1003): the bidirectional control characters U+202A to U+202E and U+2066 to U+2069 [11]; the separators U+2028 and U+2029, which some editors render as line breaks; and every code point with the Unicode property `Default_Ignorable_Code_Point` [12] (for example, U+200B, U+200D, and U+2060). In a string they are written as escapes (`\u{200D}`). This rejects invisible characters and the bidirectional-control variant of the Trojan Source attacks that Boucher and Anderson describe [13] (CVE-2021-42574 [14]). Homoglyphs inside string literals and comments (the CVE-2021-42694 class [15]) are not rejected (section 22).
- **LS-7.** Control characters other than horizontal tab (U+0009), line feed (U+000A), and a carriage return immediately followed by a line feed must not appear unescaped (C1004).

**LS-8.** **Proposed.** A file may begin with `profile "cint-core-1";`. If present, it must name a language profile that the compiler implements. If absent, `.ci` files are `cint-core-1`.

### 3.2 Lines, columns, and whitespace

**Specified.**

- **LS-9.** A line ends at LF or at CR LF. A lone CR is rejected (C1004).
- **LS-10.** Lines are numbered from 1. Columns are numbered from 1 and count Unicode scalar values (definition D76 of the Unicode Standard [16]) from the start of the line; a tab counts as one column. This is the column of SPEC-01 IM-106.
- **LS-11.** Whitespace (space, tab, line ends) separates tokens and is otherwise insignificant. Indentation has no meaning.
- **LS-12.** Tokens are formed by maximal munch: the longest sequence that forms a valid token is taken. `a+%b` is `a`, `+%`, `b`. `x<<%2` is `x`, `<<%`, `2`.

### 3.3 Comments

**LS-13.** **Specified.** `//` begins a comment that ends at the end of the line. `/*` begins a block comment that ends at the matching `*/`.

**LS-14.** **Proposed.** Block comments nest: `/* a /* b */ c */` is one comment. An unterminated block comment is C1005. `///` begins a documentation comment that attaches to the next declaration.

### 3.4 Identifiers and keywords

**Specified.**

- **LS-15.** An identifier matches `[A-Za-z_][A-Za-z0-9_]*`, is case-sensitive, and is at most 255 bytes. A longer identifier is C1012 on every implementation (section 20).
- **LS-16.** `_` alone is the discard name. It can be assigned (`_ = f();`) and bound in destructuring, never read.
- **LS-17.** Identifiers that match `[IUQT][0-9]+` are reserved for type names, whether or not a type of that name exists (C1010). `Q32` is therefore not a valid variable name.
- **LS-18.** Keywords (reserved, cannot be identifiers):

```text
as          assert      break       by          case        catch
const       continue    default     defer       do          else
enum        errdefer    error       expect_fault export     fallthrough
false       for         if          import      in          in_place
inout       kernel      out         over        profile     reduce
return      schedule    static_assert struct    switch      test
true        try         type        var         void        where
while
```

- **LS-19.** Reserved for Proposed features (cannot be identifiers): `distinct` (section 4.8), `secret` (section 17.1, SPEC-07 SEC-CT-1).
- **LS-20.** `round` is a contextual keyword. It is a keyword only immediately after the target type of a conversion (`x as I32 round floor`) and immediately after a fraction literal (`0.1 round half_even`). Elsewhere it is an ordinary identifier.
- **LS-21.** Built-in type names: `I8 I16 I32 I64 U8 U16 U32 U64 I128 I256 I512 I1024 Bool Str T1 T27`, every fixed-point name of the form `Q<i>.<f>` (section 3.6), and (spelling Proposed, owned by SPEC-05 and SPEC-03) `PT5 PT4 Arena Pool Handle`. The built-in error sets are `ArithError` (SPEC-01 IM-188), `AllocError` (SPEC-03 M-19), `FormatError` (LS-216), and `Utf8Error` (section 6.8). Every built-in type name starts with a capital letter, so a type never reads as a value, and the types a program declares, structs such as `Body` and error sets such as `IoError`, follow the same convention with nothing added to the grammar. The spelling is unfamiliar on first reading to a reader used to lowercase type names; the cost is not a standing one.
- **LS-22.** Built-in function names (section 6.8) are not keywords, but they are defined in the implicit prelude and must not be redefined (one definition per name, section 11).
- **LS-23.** Fault names (`E_OVERFLOW` and the others) are identifiers resolved against the fault table of SPEC-01 IM-104, so a fault code added there (for example `E_DOMAIN`) needs no grammar change. An `E_` name that is not in that table is C1011.

### 3.5 Integer literals

**LS-24.** **Specified.**

| Form | Pattern | Example | Value |
|---|---|---|---|
| Decimal | `0`, or a nonzero digit then digits | `1_000_000` | 1000000 |
| Hexadecimal | `0x` then hex digits (either case) | `0xDEAD_beef` | 3735928559 |
| Binary | `0b` then `0` or `1` | `0b1010_0101` | 165 |
| Octal | `0o` then `0` to `7` | `0o755` | 493 |
| Balanced ternary | `0t` then `N`, `0`, `P` | `0tP0N` | 8 |

Rules:

1. **LS-25.** The prefix letter is lowercase. `0X1F` is C1020, with the fix `0x1F`.
2. **LS-26.** A single `_` may appear between two digits. It may not lead, trail, follow the prefix, or repeat: `1__0`, `1_`, `0x_1F`, and `_1` (which is an identifier) are not valid literals. `1__0` and `1_` are C1021.
3. **LS-27.** A decimal literal of more than one digit must not begin with `0`: `007` is C1022, with the fix `7` or `0o7`. This removes C's silent octal, in which a leading `0` makes an integer constant octal (ISO/IEC 9899:2018, section 6.4.4.1 [1]).
4. **LS-28.** A prefix `-` in operand position followed by a numeric or character literal forms a single negative literal (SPEC-01 IM-25), whatever whitespace or comments separate the two tokens: `-128`, `- 128`, and `-/* c */128` are each the value -128 in context `I8`, never the negation of an `I8` 128. A `-` before any other expression, including a parenthesized literal or a negative literal, is negation: `-(128)` and `- -128` are negations. (The C# specification likewise states its negative-literal case for a literal that is the token immediately following a unary minus token [17], and C# whitespace and comments separate tokens without otherwise affecting syntax [18].)
5. **LS-29.** There are no type suffixes. A literal's type comes from context (section 4.2). `5 as I32` names a type explicitly.
6. **LS-30.** A balanced-ternary literal lists the most significant trit first; `N` is -1, `0` is 0, and `P` is +1. Balanced ternary is the radix-3 notation with digits -1, 0, and +1 that Knuth describes in section 4.1 of *The Art of Computer Programming* [19]. `0tN` is -1. Its meaning is an ordinary integer literal, typed by context; the `T27` type and packed trit encodings are defined by SPEC-05.
7. **LS-31.** A literal denotes an exact value whose magnitude is at most 2^4096 - 1. A larger literal is C1023, reported at its first character, on every implementation; it is never truncated.

Boundary cases:

```ci
I8  a = -128;          // valid: a negative literal; -128 fits I8
I8  b = 128;           // C2003: 128 does not fit I8 (-128 ..= 127)
U64 c = 0xFFFF_FFFF_FFFF_FFFF;   // valid: 18446744073709551615
I64 d = 0xFFFF_FFFF_FFFF_FFFF;   // C2003: no reinterpretation; write -1 or use as%
I64 e = 0;             // valid: a lone 0 is decimal
I8  f = - 128;         // valid: a negative literal; the space does not matter (LS-28)
```

`-(128)` is never a negative literal, however it is spaced. The parentheses make `128` an operand of its own, typed in the context before the minus applies. In context `I8`, `I8 a = -128;` and `I8 a = - 128;` are -128, but `I8 a = -(128);` is C2003 at the `128`, because 128 is not an `I8`. In a wider type the value can match while the typing differs: `I64 e = -(128);` is the negation of an `I64` 128. `U8 u = -(1);` is C6001 `E_OVERFLOW` `neg.checked.u8` with exact -1, while `U8 u = -1;` and `U8 u = - 1;` are C2003 at the `-`. As a conversion source, `-(128) as I8` is C2101 (LS-151); write `-128 as I8`.

**LS-32.** **Proposed.** Exponent notation for exact integers: `1e9` is the integer literal 1000000000. A form whose value is not an integer (`1e-3`) is valid only as a fraction literal (section 3.6).

### 3.6 Fixed-point literals and type names

**Specified** (aligned with SPEC-01 IM-19, IM-27 and IM-54).

- **LS-33.** A decimal fraction literal has digits on both sides of the point: `1.5`, `0.25`, `100.0`. `1.` and `.5` are not literals. A hexadecimal fraction literal is `0x` hex digits `.` hex digits: `0x1.8` is 3/2. Digit separators follow section 3.5 on each side.
- **LS-34.** Because a fraction requires a digit after the point, `1..5` lexes as `1`, `..`, `5`, and `0x1..5` lexes as `0x1`, `..`, `5`.
- **LS-35.** A fraction literal denotes an exact rational (section 4.2 rule 1). It may receive only a fixed-point type. `I64 x = 1.5;` is C2004.
- **LS-36.** A fixed-point type name is spelled `Q<i>.<f>` with no spaces, for example `Q32.32` (signed, 64-bit storage, 32 fractional bits), `Q16.16` (signed, 32-bit storage), and `Q8.0`. The lexer recognizes `Q`, digits, `.`, digits as one token. `i + f` must be 8, 16, 32, or 64, `i >= 1` and `f >= 0` (C2005). `i` counts the sign bit. Storage, intermediate width, rounding, and overflow are defined by SPEC-01 section 5.
- **LS-37.** When a fraction literal receives a type `Q<i>.<f>`, its value `v` must satisfy: `v * 2^f` is an integer within the storage range. Otherwise the program is rejected: C2006 if `v * 2^f` is not an integer, C2003 if it is out of range. A `round` clause after the literal rounds once to the grid with the named mode, then checks range: `Q16.16 t = 0.1 round half_even;` gives raw 6554 (SPEC-01 IM-27). An expression of fraction literals is typed first and evaluated with fixed-point arithmetic (section 4.2 rule 2).
- **LS-38.** `x.raw` yields the storage integer of a fixed-point value (type `I32` for `Q16.16`). `Q16.16.raw(n)` constructs a value from a storage integer of exactly the storage type. Both are exact and never fault (SPEC-01 IM-57).

```ci
Q16.16 half  = 0.5;                    // raw 32768
Q16.16 tenth = 0.1;                    // C2006: 0.1 is not a multiple of 2^-16
Q16.16 t2    = 0.1 round half_even;    // raw 6554
Q16.16 h2    = 0x1.8;                  // raw 98304 (1.5)
Q8.0   whole = 100.0;                  // raw 100; f = 0 is valid
Q8.0   big   = 128.0;                  // C2003: Q8.0 holds -128 ..= 127
```

### 3.7 Character literals

**LS-39.** **Specified.** A character literal is one Unicode scalar value in single quotes, and is an integer literal equal to its code point: `'A'` is 65, `'é'` is 233, `'\u{1F600}'` is 128512. `''` and `'ab'` are C1030. Escapes are those of section 3.8, with the `Str` rule for `\xHH`: 00 to 7F, so `'\x41'` is 65 and `'\xff'` is C1032.

### 3.8 String and byte-string literals

**Specified.**

- **LS-40.** A string literal `"..."` has type `Str` (section 4.1): a read-only view of UTF-8 bytes. A literal's bytes have static lifetime. There is no terminator; `len("abc")` is 3 and `"a\0b"` has length 3 with an ordinary zero byte in the middle.
- **LS-41.** A byte-string literal `b"..."` has type `in U8[_]` (a read-only byte view of static storage) and is not required to be valid UTF-8.
- **LS-42.** Escapes:

| Escape | Bytes | Allowed in |
|---|---|---|
| `\\` `\"` `\'` | the character | both |
| `\n` `\r` `\t` `\0` | 0x0A 0x0D 0x09 0x00 | both |
| `\xHH` | one byte | `Str`: 00 to 7F only; `b"..."`: 00 to FF |
| `\u{H...}` | UTF-8 of the scalar value, 1 to 6 hex digits | `Str` only; a value that is not a Unicode scalar value (a surrogate U+D800 to U+DFFF, or a value above U+10FFFF) is C1031 |
| `{{` `}}` | `{` `}` | both |

- **LS-43.** Any other backslash sequence is C1032.
- **LS-44.** A literal `{` or `}` must be written `{{` or `}}` in every string literal, so that a string means the same thing wherever it appears. A single `{` begins an interpolation hole. Holes are permitted only in print statements, `format` calls, and `assert` messages (section 9). Test names are never interpolated. A hole elsewhere is C1033.
- **LS-45.** Adjacent string literals are concatenated at compile time: `"ab" "cd"` is `"abcd"`.
- **LS-46.** A string literal may not contain an unescaped line break (C1034).
- **LS-47.** `Str` values compare with `==` and `!=` byte for byte. Indexing a `Str` yields `U8`. Slicing a `Str` (section 7.6) yields `Str` and faults with `E_BOUNDS` if either end falls inside a multi-byte UTF-8 sequence.

**Open.** Raw strings and multi-line string literals.

### 3.9 Operators and punctuation

**LS-48.** **Specified** (except where marked). The complete operator and punctuation set:

```text
+   -   *   /   %   <<  >>          checked arithmetic and shifts
+%  -%  *%  <<%                     wrapping forms
+|  -|  *|                          saturating forms
&   |   ^   ~                       bitwise
==  !=  <   <=  >   >=              comparison (not chainable, section 7.3)
&&  ||  !                           logical (Bool only)
?   :                               conditional expression
=   +=  -=  *=  /=  %=  <<=  >>=  &=  |=  ^=
+%= -%= *%= <<%= +|= -|= *|=        assignment forms (statements only)
++  --                              increment statements
as  as%                             conversions
as|                                 saturating conversion (Proposed)
as?                                 result-returning conversion
..  ..=                             ranges
(   )   [   ]   {   }   ,   ;   .   @   !  (in error-union types)
```

**LS-49.** `as%`, `as|`, and `as?` are single tokens with no internal space.

## 4. Types

### 4.1 Scalar and built-in types

**LS-50.** **Specified** (spelling). SPEC-01, SPEC-03, and SPEC-05 own the semantics.

| Type | Values | Storage | Alignment | Notes |
|---|---|---|---|---|
| `I8` `I16` `I32` `I64` | two's complement signed | 1, 2, 4, 8 bytes | equal to size | `I64` is the canonical machine word. |
| `U8` `U16` `U32` `U64` | unsigned | 1, 2, 4, 8 bytes | equal to size | |
| `I128` `I256` `I512` `I1024` | signed, fixed capacity | 16 to 128 bytes | 8 | Little-endian 64-bit limb layout (SPEC-01 IM-12). |
| `Q<i>.<f>` | signed fixed point | `i + f` bits | that of its storage integer | Section 3.6. |
| `Bool` | `true`, `false` | 1 byte, 0 or 1 | 1 | Not an integer type. |
| `Str` | UTF-8 byte view | view | not storable | Sections 3.8 and 5.1. |
| `T1` | one trit: -1, 0, +1 | 1 byte (SPEC-05 TR-T1-6) | 1 | |
| `T27` | 27 balanced trits | 8 bytes, `I64` carrier (SPEC-03 A-13) | 8 | |
| `PT5[...]`, `PT4[...]` | packed trit arrays | SPEC-05 5.1 | 1 | Spelling Proposed (SPEC-05). |
| `Arena`, `Pool(T)`, `Handle(T)` | memory objects | SPEC-03 3.2 | SPEC-03 | Spelling Proposed. Built-in parameterized types; user generics are Open (section 21). |

**LS-51.** Every numeric type `T` has the compile-time properties `T.min`, `T.max`, and `T.bits`; fixed-point types also have `T.frac_bits` and `T.epsilon` (the value whose raw storage is 1). `I8.max` is 127. `Q16.16.epsilon` prints as `0.0000152587890625`.

### 4.2 Literals and context typing

**LS-52.** **Specified.** SPEC-01 3.2 owns the meaning; this section owns the spelling. Literal-only expressions are not folded exactly in Z: SPEC-01 IM-23 and IM-103 require run-time semantics at compile time.

1. **LS-53.** **Literals.** An integer, character, or fraction literal denotes an exact value (in Z, or a rational for a fraction) and has no type of its own. It receives a type from its context (rule 4) and must be exactly representable in that type: C2003 otherwise, or C2006 for a fraction that is not on the grid of its fixed-point type unless a `round` clause follows the literal (section 3.6). A prefix `-` in operand position before a literal, with or without whitespace or comments between them, is part of the literal (LS-28).
2. **LS-54.** **No untyped arithmetic.** An operator expression whose operands are all literals is typed first, exactly as if it were evaluated at run time: the context type passes to its operands. It is then evaluated with the run-time meaning of SPEC-01 sections 4 and 5. Checked, wrapping, and saturating forms, shift-count rules, and fixed-point rounding apply unchanged, and no whole expression is evaluated in Z. Folding at compile time never changes a value or a fault; a fault during that evaluation is C6001 carrying the fault code and the exact operands (SPEC-01 IM-23, IM-103). A literal operand (a literal, a negative literal, or a literal operand in parentheses, SPEC-01 IM-26) that is the source operand of `as`, `as%`, `as|`, or `as?` is converted from its value in Z (`-1 as% U64`, `(18446744073709551615) as U64`); any other literal-only source expression is typed `I64` before conversion. The unit of compile-time evaluation is the maximal constant expression; inside it, `&&`, `||`, and `?:` evaluate only the operands the result requires, and a skipped operand is type-checked and range-checked but not evaluated; a constant operand that `&&`, `||`, or `?:` of a non-constant expression may skip is evaluated only at run time, if reached; and a checked conversion of a literal is decided at compile time even in a skipped operand (SPEC-01 IM-23). So `const Bool B = false && (1 / 0 == 0);` is `false`, and `Bool b = x == 2 && (1 / 0 == 0);` compiles and faults `E_DIV_ZERO` when run with `x` equal to 2.
3. **LS-55.** **Named constants are typed.** Every `const` declaration has a declared type (`const I64 N = 1024;`). A `const` name is an ordinary typed operand and never takes its type from context. There are no untyped named constants (SPEC-01 IM-24).
4. **LS-56.** **Context.** A literal receives a type from the first applicable rule:
   1. the declared type of the variable, constant, field, or place it initializes or is assigned to;
   2. the parameter type of the argument position it fills, for a user function;
   3. the declared result type, in a `return` statement;
   4. the type of the other operand of a binary arithmetic, bitwise, or comparison operator whose other operand is typed, except that a shift count is never typed by this rule;
   5. the element type of an array literal of declared element type;
   6. the scrutinee's type (the scrutinee is the value a `switch` tests), for a `case` item;
   7. for an argument of a polymorphic built-in (section 6.8), the operand type that the built-in's type signature unifies from (that is, infers as the one type shared by) its typed arguments and type arguments; if none is typed, the declared type that the call's result initializes, when the type signature's result type is the operand type;
   8. otherwise `I64`.
5. **LS-57.** **Shift counts.** The right operand of `<<`, `<<%`, and `>>` may have any integer type and is compared as a value in Z (SPEC-01 IM-45). A literal count is typed `I64`. The left operand of a shift is typed by the context of the whole shift under rules 1 to 3 and 5 to 8 of LS-56, never by rule 4 from the other operand of an enclosing operator and never by the count (SPEC-01 IM-22). So with `I8 x`, `I8 y = x + (1 << 6);` is C2001 (`I8 + I64`): the shift is typed `I64`. A count outside `0 ..= T.bits - 1` faults `E_SHIFT` at run time, and is C6001 when the whole expression is evaluated at compile time.
6. **LS-58.** **Representability.** `I8 x = 0; x + 300` is rejected (C2003: 300 does not fit `I8`); `x + 100` is valid and is checked at run time.
7. **LS-59.** **Comparisons** give a literal operand the type of the typed operand. `I8 x = 0; x < 1000` is C2003. The fix is `(x as I16) < 1000`.
8. **LS-60.** **Enum literals** (`.burn`), error values (`.overdrawn` where the set is known), and context-typed `Round` values (`.half_even` for a parameter of type `Round`) are typed by context in the same way. With no context they are C2007.

```ci
const I64 LIMIT = 1 << 40;      // typed constant: 1099511627776
I32 small = LIMIT;              // C2001: I64 does not convert implicitly to I32
I64 big   = LIMIT;              // valid
I16 m     = -32768;             // valid: a negative literal
I16 m2    = -(1 << 15);         // C6001: E_OVERFLOW; 1 << 15 is evaluated in I16, exact 32768
U8  u     = -1;                 // C2003: -1 does not fit U8 (0 ..= 255)
U64 all   = -1 as% U64;         // valid: 18446744073709551615 (conversion of the value -1)
I16 h     = 40000 - 30000;      // C2003: the literal 40000 does not fit I16
I32 v     = 2_000_000_000 * 2 / 4;   // C6001: E_OVERFLOW at *, exact 4000000000

I8  p   = 100 +% 100;           // valid: -56, wrapping in I8
I64 one = 1;
I64 w   = one <<% 63;           // valid: -9223372036854775808
U8  s   = 200 +% 100;           // valid: 44, wrapping in U8
I64 bad = 1 << -1;              // C6001: E_SHIFT, count -1

U8  b8  = 1;
I64 k   = 3;
U8  sh  = b8 << k;              // valid: count may be I64 (rule 5); result 8
U8  far = b8 << 300;            // valid at compile time; faults E_SHIFT (count 300, width 8) when run

Q16.16 third = 1.0 / 3.0;                    // valid: raw 21845; fixed-point / rounds half_even (SPEC-01 IM-66)
Q16.16 q     = (1.0 / 3.0) * 3.0;            // raw 65535: the quotient rounds, then the product (SPEC-01 fixture 100)
Q16.16 tenth = 0.1;                          // C2006: 0.1 is not on the Q16.16 grid
Q16.16 t2    = 0.1 round half_even;          // raw 6554
Q16.16 mix   = 1 / 3.0;                      // valid: raw 21845; the literal 1 is typed Q16.16 (rule 4.4)
I64    fl    = 7 / 2;                        // 3: floor division
I64    neg   = -7 / 2;                       // -4: the negative literal -7, floor division
```

### 4.3 Arrays and views

**LS-61.** **Specified** (surface). The view model is owned by SPEC-02 section 3 and SPEC-03.

- **LS-62.** An array type is an element type followed by a shape: `I64[8]`, `I32[h, w]`, `Q16.16[n]`. Extents in a global declaration and in a kernel body must be compile-time constants. In sequential function bodies an extent may also be a size parameter or an `I64` expression of size parameters; such arrays are allocated as described in section 5.1. An extent that evaluates below 0 faults `E_SHAPE` at the declared name, operation `decl.shape`, with operands the dimension (from 0) and the requested extent as `I64`, no `exact`, and `limit` `I64 0`, after every extent of the declaration is evaluated left to right; a negative constant extent is C2015.
- **LS-63.** Storage of a declared multi-dimensional array is row-major: the last index varies fastest. Views over other layouts carry strides.
- **LS-64.** Indexing is 0-based unless a lower bound is declared. `m[i, j]` indexes a rank-2 array. An index has type `I64` (a literal index is typed `I64`; an index of another integer type is converted explicitly, `a[k as I64]`). Each index is checked against its extent; a violation faults with `E_BOUNDS` and reports the index and the extent and, at rank 2 and above, the dimension first, counted from 0; of several out-of-range indices the lowest dimension is reported (SPEC-01 IM-186).
- **LS-65.** **Partial indexing.** Indexing a rank-`r` array or view with `k < r` indices yields a view of rank `r - k` of the remaining dimensions, with the permission of the indexed expression: for `I32[2, 8, 8] p`, `p[0]` is the same view as `p[0, .., ..]`. Indexing the result of a partial index directly, `m[i][j]`, is C2010 with the fix `m[i, j]`.
- **LS-66.** `_` in a shape accepts any extent for that dimension in a parameter or view declaration: `in I64[_] xs`. `I64[]` is shorthand for `I64[_]`.
- **LS-67.** An array declared without an initializer is filled with zeros, subject to section 4.5 for enum elements. An array literal `[a, b, c]` must have exactly as many elements as the extent (C2011); nested literals give rank 2 and above: `I32[2, 2] m = [[1, 2], [3, 4]];`. A trailing comma is permitted. Brace literals (`{1, 2}`) are not array literals.
- **LS-68.** `I64[0] none;` is valid. Any index into it faults with `E_BOUNDS`.
- **LS-69.** **Owned versus view.** A declaration with a type and no permission keyword declares owned storage. If it has an initializer that is an array or view expression, the elements are copied into the new storage: `I32[8, 8] left = m[.., 0..8];` is an owned copy, and a later write to `left` does not reach `m`. A view is declared only with `in` or `inout` (section 5.1): `inout I32[8, 8] left = m[.., 0..8];` aliases `m`.
- **LS-70.** **Assignment and copy.** Array assignment `b = a;` and `copy(dst, src)` copy every element in row-major logical order. Shapes must match (C2012 when known statically, `E_SHAPE` otherwise). The run-time shape fault for assignment, an array initializer, and `copy(dst, src)` is `copy.shape` with the record and ordering of SPEC-01 IM-187. Source and destination must be proved disjoint by the tests T0 to T2 of SPEC-02 A-5, or be exactly the same view (test T3, in which case the copy has no effect). Any other pair is C5010 when provable at compile time and `E_ALIAS` at run time otherwise. The run-time fault is operation `copy.alias` with no operands, no `exact`, and no `limit`, at the `=` or the callee name `copy`, after the shape check and before any element write (SPEC-01 IM-187). No implementation inserts a hidden temporary. To move overlapping data, copy through storage the program declares:

```ci
I64[16] x;
I64[8]  tmp;
x[0..8 by 2] = x[0..4];       // C5010: same buffer, bounding intervals intersect, gcd test fails; the views share offsets 0 and 2 (E_ALIAS if known only at run time)
tmp[0..4] = x[0..4];          // valid: distinct buffers (T1)
x[0..8 by 2] = tmp[0..4];     // valid
x[0..8] = x[0..8];            // valid: identical view (T3), no effect
```

- **LS-71.** Array `==` is not defined in `cint-core-1` (C2013). The built-in `equal(a, b)` returns `Bool`.

**LS-72.** **Proposed.** Declarable lower bounds for ported code. A dimension is written as a range with the global meaning of `..` and `..=` (section 7.6): `I64[1..=100] a;` and `I64[1..101] a;` both have extent 100 and valid indices 1 to 100. In a parameter, `in I64[1..=m, 1..=n] a` admits `a[1, 1]` through `a[m, n]`. `lower(a, d)` returns the lower bound of dimension `d`. An array slice of a lower-bounded array has, in each dimension, the parent's declared lower bound (0 for an undeclared one). For a 1-based parent, this is Fortran's rule that an array section has lower bound 1, and that an assumed-shape dummy argument with no declared lower bound has lower bound 1 (ISO/IEC 1539-1:2018, sections 16.9.109 and 8.5.8.3 [20]):

```ci
// Fortran: real :: a(100); call f(a(5:10)); inside f, an assumed-shape dummy real :: a(:) has bounds 1:6
I64[1..=100] a;
inout I64[_] s = a[5..=10];   // extent 6; valid indices s[1] ..= s[6]; s[1] is a[5]
```

### 4.4 Structs, layout, and sub-word access

**Specified.**

```ci
struct Body {
    I64    id;
    Q32.32 mass;
    I32[3] pos_mm;
    U16    flags;
}
```

- **LS-73.** Fields are laid out in declaration order. Each field is placed at the next offset that is a multiple of its alignment (section 4.1). The struct's alignment is the largest field alignment, and its size is rounded up to a multiple of its alignment. The compiler never reorders fields. An array has the alignment of its element; an enum, that of its underlying type; a nested struct, its own alignment.
- **LS-74.** Padding bytes are zero in memory and never observable from source. Canonical serialization encodes fields only and contains no padding bytes (SPEC-01 IM-146 tag `71`, 11.3).
- **LS-75.** `@packed` (section 13) gives alignment 1 and no padding.
- **LS-76.** `T.bytes` and `offset(T, field)` are compile-time constants. `static_assert(Body.bytes == 32);` holds for the struct above: 8 + 8 + 12 + 2 = 30, rounded up to 32.
- **LS-77.** A struct value is constructed by naming the type like a call: `Body(7, 1.5, [0, 0, 0], 0)` positionally, or `Body(id = 7, mass = 1.5, pos_mm = [0, 0, 0], flags = 0)` by name. Every field must be supplied (C2020) unless it declares a default (Proposed: `U16 flags = 0;`). `Body{ ... }` is not a constructor. Copying an array or view argument into an array field uses the `copy.shape` rule of SPEC-01 IM-187: all arguments are evaluated first, then shapes are checked in field declaration order before any field is copied. A run-time mismatch is at the constructor callee name; a statically known mismatch remains C2012.
- **LS-78.** Assignment copies the whole struct.
- **LS-79.** A struct field must not have type `Str` or any view type (C2051).

**LS-80.** **Proposed.** For structs that contain only 8-bit to 64-bit integer, `Bool`, enum, and fixed-point fields, and no bit fields, this layout equals the C layout of the corresponding `int8_t`..`int64_t` struct on the x86-64 System V [21], Windows x64 [22], and AArch64 (AAPCS64 [23]) ABIs. The claim becomes Specified when the conformance suite checks it with generated `static_assert` on size and every field offset for each of those ABIs (as SPEC-08 8.3 does for its records). Layout of `I128` and wider fields at the ABI is governed by SPEC-03.

**LS-81.** **Bit fields (Specified).** A field may declare a bit width with `:`.

```ci
struct Status {
    U16  mode  : 3;
    Bool armed : 1;
    I16  trim  : 12;      // signed field, two's complement within 12 bits
}
static_assert(Status.bytes == 6);
```

**LS-82.** Consecutive bit fields of the same declared type share one storage unit of that type, allocated from the least significant bit upward. A field of a different declared type, or one that would cross the end of the current unit, starts a new unit. `Status` therefore has three units: `U16` at offset 0, `Bool` at offset 2, `I16` at offset 4, size 6. This allocation is specified independently of any C compiler's bit-field rules. Under the x86-64 System V ABI, which lets a bit field share a storage unit with other members [21], a direct C transcription would occupy 2 bytes; this has not yet been measured with GCC or Clang. A field's width must be at least 1 and at most its type's width; a `Bool` field must have width 1. Reading yields the declared type (signed fields are sign-extended). Writing a value that does not fit the width faults with `E_NARROW`.

**LS-83.** Wrapping into a field is explicit. For an unsigned field, mask: `s.mode = v & 0b111;`. For a signed field, masking does not wrap (`3000 & 0xFFF` is 3000, which faults `E_NARROW` for a 12-bit signed field); use `wrap_bits(v, 12)` (Proposed), which returns `v` reduced modulo `2^12` into the signed range `-2048 ..= 2047` in the type of `v`: `wrap_bits(3000, 12)` is -1096.

**LS-84.** A struct with bit fields crosses the C ABI only as its raw storage units, with generated accessor functions; it is never mapped to C bit fields (SPEC-03 A-13 governs the header).

**LS-85.** **Sub-word access (Proposed).** On any integer value `x` of `w` bits, defined on its two's complement storage and independent of host byte order:

| Expression | Type | Meaning |
|---|---|---|
| `x.bits[lo..hi]` | unsigned of `x`'s width | Bits `lo` up to but excluding `hi`, shifted down to bit 0. `0 <= lo < hi <= w`, else `E_BOUNDS`. |
| `x.byte[k]` | `U8` | Byte `k`, where byte 0 is least significant. |
| `x.lanes(U16)[k]` | `U16` | Lane `k` of width 16, lane 0 least significant. The lane width must divide `w`. |

**LS-86.** All three are assignable when `x` is a mutable place. Assigning a value outside the field's range faults with `E_NARROW`.

```ci
U32 w = 0x1234_5678;
"{w.byte[0]:#x} {w.bits[28..32]:#x}\n";   // prints: 0x78 0x1
w.lanes(U16)[1] = 0xBEEF;
"{w:#x}\n";                               // prints: 0xbeef5678
```

### 4.5 Enums

**Specified.**

```ci
enum Mode : U8 { idle, burn = 4, coast }   // idle = 0, burn = 4, coast = 5
```

- **LS-87.** An enum declares its underlying integer type after `:`; it is required (C2030). Values start at 0 and increase by 1 unless assigned. Duplicate values and values that do not fit the underlying type are C2031.
- **LS-88.** Enumerators are written `Mode.burn`, or `.burn` where the type is known from context.
- **LS-89.** An enum is not an integer. `m as U8` gives its value; `x as Mode` faults with `E_NARROW` if `x` is not the value of an enumerator.
- **LS-90.** A `switch` over an enum without `default` must list every enumerator (C4020).
- **LS-91.** **Zero fill.** Zero-filling (section 5.1) produces the enumerator whose value is 0. If an enum has no enumerator with value 0, any declaration that would zero-fill a value of that enum, directly or inside an array or struct, must have an initializer (C2054). This keeps every enum value an enumerator, so an exhaustive `switch` always matches. Error-set values follow the same rule, because no error value is 0 (LS-314).

```ci
enum Gear : U8 { low = 1, high = 2 }
struct Box { Gear g; }
Box b;                      // C2054: Gear has no zero enumerator; write Box b = Box(.low);
Gear[4] gs;                 // C2054
Mode[4] ms;                 // valid: every element is Mode.idle
```

### 4.6 Error sets and error unions

**Specified.**

```ci
error ParseError { empty, bad_digit, too_large }
error IoError : U8 { closed, full }

ParseError!I64 parse_u(Str s) { ... }
ParseError!void check(Str s) { ... }
```

The spelling of error sets, error unions (`E!T`), `try`, `catch`, `defer`, and `errdefer` (section 8.7) is adapted from Zig [24]. The meaning is the one defined in this document.

- **LS-92.** `error Name { ... }` declares a set of error values. Error values are written `ParseError.bad_digit` or, where the set is known, `.bad_digit`.
- **LS-93.** An error set has an underlying unsigned type, written after `:` as for enums; the default is `U16`. Error values are numbered from 1 in declaration order; 0 is reserved for success. Adding a value at the end of a set keeps every earlier number. The C layout of `E!T` is a struct of the tag (the underlying type, 0 on success) followed by the success value under the struct layout rules of section 4.4; SPEC-03 A-13 maps it to the generated header. An error value is its tag alone and carries no payload; detail that a caller needs travels in an argument the caller passes, such as an `inout` struct.
- **LS-94.** `E!T` is the type "either a `T` or a value of error set `E`". `E!void` carries no success value. `E!(T1, T2)` carries several (section 4.7).
- **LS-95.** In `cint-core-1`, an error union is valid only as a function result type. A call returning one must be consumed immediately by `try`, `catch`, or `_ =` (C2040 otherwise). To branch on the error, bind it and switch on the error value: `f() catch (e) { switch (e) { ... } }`.
- **LS-96.** Errors are ordinary values for expected, recoverable conditions. Faults are something else (section 6.5). SPEC-01's result-returning forms (`add_result`, `as?`, and the others) return `ArithError!T`, where `ArithError` is the built-in set of SPEC-01 IM-188, and are consumed the same way.
- **LS-314.** (Specified.) A value of an error set is an ordinary value, like an enum value (section 4.5). A variable, a struct field, an array element, a parameter, and a function result may have an error-set type, so a batch job can record why each input failed. Error-set values compare with `==` and `!=`, and a `switch` over one without `default` lists every value of the set (LS-182). No error value is 0, so a declaration that would zero-fill an error-set value, directly or inside an array or struct, needs an initializer (C2054, LS-91). Error unions stay function results only (LS-95), so every error is handled or discarded at its call.

**LS-97.** **Specified.** Combined sets: `error IoOrParse = IoError | ParseError;`, or, with a declared underlying type, `error IoOrParse : U32 = IoError | ParseError;`. Each operand is an error set, itself combined or not (C3007 otherwise). A combined set is numbered by concatenation: the first operand's members keep their numbers, and the second operand's members follow in their own order; a set that appears twice is included once, at its first position. Every declared set keeps its own numbering, and `try` from a member set converts the tag through a table fixed at compile time. The combined set's underlying type is declared or defaults to `U16`; a set too large for it is C2031, as for an enum. The values of a combined set are the values of its member sets: `ParseError.bad_digit` is one value, which a `ParseError` holds with tag 2 and an `IoOrParse` with tag 4. With the sets above, `IoOrParse` numbers `IoError.closed` 1, `IoError.full` 2, `ParseError.empty` 3, `ParseError.bad_digit` 4, and `ParseError.too_large` 5.

**Specified.** Where the set is known, `.name` and `IoOrParse.name` name the value called `name` when exactly one member set declares that name. When several do, the value is written with the set that declares it, and the short form is C2044: with `error QueueError { full, stalled }` and `error IoOrQueue = IoError | QueueError;`, the two values are `IoError.full` and `QueueError.full`, and `.full` or `IoOrQueue.full` is C2044. A value of an error set converts to any error set that holds all of its values, through the same table: at `try`, at `return`, in an assignment or initializer, as an argument, and as an operand of `==` or `!=` whose other operand has the wider set. It never converts the other way: a value of `IoOrParse` is not a `ParseError` (C2042 at `try` and `return`, C2001 elsewhere).

### 4.7 Tuples for multiple returns

**LS-98.** **Specified.** A function may return several values by declaring a parenthesized type list. Tuples exist only as return types and are consumed by destructuring.

```ci
(I64, I64) divmod_floor(I64 a, I64 b) {
    return (a / b, a % b);
}

(I64 q, I64 r) = divmod_floor(-7, 2);   // declares q = -4, r = 1 (floor division, SPEC-01 IM-35)
(q, r) = divmod_floor(9, 4);            // assigns existing variables
(I64 head, _) = divmod_floor(9, 4);     // discards one result
var (q2, r2) = divmod_floor(9, 4);      // Proposed: types from the tuple
```

- **LS-99.** In a typed destructuring declaration each declared type must equal the corresponding tuple element type exactly (C2001 otherwise).
- **LS-100.** In a destructuring assignment, the places are evaluated and checked left to right before the right-hand side (section 6.7).
- **LS-101.** Tuples cannot be stored in variables, arrays, or fields, and cannot be nested (C2041). At the C ABI a tuple result is a struct whose fields are the elements in order, laid out by section 4.4.

### 4.8 Type aliases

**LS-102.** **Specified.** `type Cents = I64;` declares an alias. An alias and its target are the same type.

**LS-103.** **Proposed.** `type Meters = distinct I64;` declares a new type with the representation of `I64` that does not mix with `I64` without `as`. Arithmetic between two `Meters` yields `Meters`.

## 5. Declarations

### 5.1 Variables and views

**Specified.**

```ci
I64 ticks = 0;            // scalar: an initializer is required
I32[4] window;            // array: zero-filled
I64[16] window2;          // array: zero-filled
Body b;                   // struct: zero-filled
var total = ticks + 1;    // Proposed: type from the initializer (I64)
in I64[_] head = window2[0..8];   // read-only view (section 7.6)
inout I64[_] tail = window2[8..]; // writable view
Str name = "probe";       // Str is always a read-only view; no keyword
```

- **LS-104.** A scalar or `Str` variable without an initializer is C2050. There is no uninitialized read anywhere in the language.
- **LS-105.** `var name = expr;` (Proposed) takes the type of `expr`. A literal-only initializer gives `I64` (section 4.2 rule 4.8).
- **LS-106.** **Views are second-class** (SPEC-03 M-21). A view variable is declared with a permission keyword, `in` (read-only) or `inout` (writable); `Str` is a view type that is always read-only and needs no keyword. A view or `Str`:
  - may be a local variable, a parameter, or an argument;
  - must not be stored in a struct field, an array, a global, an arena, or a pool, or captured by a `defer` that outlives its block (C2051);
  - may be returned only if it is derived from one of the function's view or `Str` parameters, or from a string or byte-string literal (static lifetime). Derivation is checked statically (C2051).
- **LS-107.** Assignment `=` to a `Str` variable rebinds it to another view. It never writes bytes. Assignment to an `in`/`inout` view variable copies elements into the viewed storage (section 4.3); a view variable cannot be rebound.
- **LS-108.** Cross-scope references use the generation-checked handles of SPEC-03, which fault with `E_STALE_HANDLE` when stale.
- **LS-109.** Locals are mutable. Parameters are not (section 6.2).

```ci
Str first_word(Str s) {                 // valid: derived from parameter s
    for i, c in s { if (c == ' ') { return s[0..i]; } }
    return s;
}
Str label() { return "probe"; }         // valid: literal, static lifetime
Str bad() {
    U8[4] buf;
    return format(buf, "abc") catch ""; // C2051: derived from local buf, which dies at return
}
```

**LS-110.** **Local array storage (Specified).** In sequential code, every local array is storage of the context's frame arena (SPEC-03), not of the native stack, except where the implementation proves that native stack storage cannot be exhausted for any execution within the semantic inputs. The frame arena's capacity is a semantic input (section 6.7). It is counted in elements: each local array charges its number of elements, and each element counts once whatever its type, so the point of exhaustion depends only on element counts and is the same on every backend; the implementation sizes native storage from the program's element types. The default and the minimum supported capacity are 2,097,152 elements (SPEC-06 1.4, section 20). Each entry starts with an empty frame arena. A local array is charged its element count when its declaration runs, after the `decl.shape` check (LS-62) and before its initializer, and is released when its block exits, however it exits, so the charge follows block nesting and recursion depth, and an array declared in a loop body is charged once per iteration and released before the next. Scalars, structs, view variables and the private arrays of a kernel are not charged; a script's top-level arrays are locals of its implicit entry (LS-219) and are charged. Exhausting it faults `E_BOUNDS` with operation `arena.alloc` and the operands of SPEC-03 M-19 at the declared name. Since no supported capacity is below the minimum, an implementation need not charge the local arrays of an entry whose frame use over every call path it proves at most 2,097,152 elements: no execution within the semantic inputs can then exhaust the arena. Runtime-sized local arrays (`I64[n] tmp;` with a size parameter `n`) are permitted only in sequential code. In kernels, private arrays must have compile-time extents of at most 4,096 bytes per work-item (SPEC-02 K-13, C5029).

### 5.2 Constants

**Specified.**

```ci
const I64 N = 1024;           // typed constant
const I64 SCALE = 1_000_000;  // every constant has a declared type (SPEC-01 IM-24)
const Q32.32 HALF = 0.5;
const I32[4] PRIMES = [2, 3, 5, 7];
```

**LS-111.** A constant's initializer is evaluated at compile time (section 14). A fault during that evaluation is a compile error that names the fault (C6001). Constants cannot be assigned or viewed as writable.

### 5.3 Scope and shadowing

**LS-112.** **Specified.** Blocks introduce scopes. A declaration must not reuse a name that is visible at that point, whether local, parameter, module-level, imported, or built in (C3001). Within a module, the names of type, struct, enum, error, function, kernel, schedule, constant, and module-level variable declarations are visible everywhere in the module regardless of declaration order. Local names, including the top-level variables of a script (section 10), are visible from their declaration to the end of their block.

### 5.4 Module-level state

**LS-113.** **Specified.** An importable module may declare module-level variables only with constant initializers (or none, giving zero), so initialization order is never observable. A non-constant initializer is C6004 (constant expression required), with the LS-312 note that the file is a module, not a script, because every top-level item is a declaration. A module-level view or `Str` variable is C2051 (a `const Str` with a literal initializer is valid). Top-level variable declarations in a script are locals of the script's implicit entry (section 10), not module-level variables. An arena or pool declaration (SPEC-03 M-20) is module-level in an importable module and in a script alike, with a constant capacity (C6004 otherwise).

## 6. Functions

### 6.1 Declaration syntax

**Specified.**

```ci
I64 add(I64 a, I64 b) {
    return a + b;
}

void log_step(I64 tick) {
    "tick {tick}\n";
}

// Size parameters bind extents and are checked at the call.
I64 dot[n](in I64[n] a, in I64[n] b) {
    I64 total = 0;
    for i in 0..n {
        total += a[i] * b[i];
    }
    return total;
}

// A view result must be derived from a view parameter (section 5.1).
in I64[_] front[n](in I64[n] a) {
    return a[0..n / 2];
}
```

- **LS-114.** The form is: result type, name, optional size parameters in brackets, parameters in parentheses, body. The result type is a type, `void`, a tuple, an error union, or a view type with its permission keyword.
- **LS-115.** Definitions may appear in any order. There are no prototypes and no headers.
- **LS-116.** A function with a non-`void` result must not be able to reach its closing brace (C4001). A `switch` that is exhaustive and returns in every arm counts as returning. Every loop counts as able to complete, whatever its condition, so `while (true) { return x; }` needs a `return` after it.
- **LS-117.** Size parameters are `I64` values bound from argument shapes. If two parameters bind the same size to different extents, the call faults with `E_SHAPE` (or is rejected with C2012 if known statically).

**LS-118.** **Proposed.** A `where` clause states requirements on size parameters of a function and is checked at the call: `I64 mean[n](in I64[n] a) where n >= 1 { ... }`. A violation faults with `E_SHAPE`. Kernels use the `where` clause of SPEC-02 K-1.

### 6.2 Parameter modes and aliasing

**LS-119.** **Specified.**

| Mode | Applies to | Meaning in functions |
|---|---|---|
| `in` (default) | all | Read-only for the duration of the call. Scalars by value; structs and arrays read-only. Whether a struct or array is passed by copy or by reference is not observable (see the alias rule). |
| `inout` | arrays, structs | Writable view or reference; writes are visible to the caller. |
| `out` | kernels, and view parameters of `extern` declarations (SPEC-03 H-5) | Section 13.2. In a function, `out` is C5001. |

- **LS-120.** A parameter cannot be assigned unless it is `inout` (C2060).
- **LS-121.** At a call, an `inout` argument must not overlap any other argument of the same call, read-only or writable. Overlap that is provable at compile time is C5010; otherwise it is checked at entry and faults with `E_ALIAS` before the body runs. That fault is operation `bind.alias` with two `I64` operands, the index of the `inout` parameter (the lower index when both are `inout`) and the index of the parameter it overlaps, and no `exact` or `limit`, at the callee name with the caller's stack; slice arguments are included. Read-only arguments may overlap each other. The same rule applies to kernels (section 13.2).
- **LS-122.** An argument that is, or is a view of, a module-level variable must not be passed in any mode to a function that writes that variable, directly or through any function it calls transitively (C5012). The compiler computes each function's transitive write set over the whole program. Because of this rule and the previous one, an `in` argument cannot change during the call, so an implementation may pass it by reference, mark it `restrict` in generated C, and hoist its loads.
- **LS-123.** Entry checks of a call run in the order of SPEC-02 F-5 restricted to the checks that apply to functions: handle generation, permission, element type and rank, shape binding and agreement, then aliasing. The first failure is the fault.

```ci
I64[8] g;
void bump(in I64[_] a) { g[0] = 1; }
void main() {
    bump(g[..]);                  // C5012: bump writes g, which the argument views
}
```

The call is inside `void main()` because a top-level statement would make the file a script, whose first error would be C3004, not C5012 (section 10).

### 6.3 Default and named arguments

**Specified.**

```ci
Q32.32 lerp(Q32.32 a, Q32.32 b, Q32.32 t = 0.5, Round mode = .half_even) { ... }

lerp(x, y);                          // t = 0.5, mode = .half_even
lerp(x, y, 0.25);
lerp(x, y, mode = .floor);           // named argument skips t
```

- **LS-124.** Parameters with defaults must follow all parameters without defaults (C2061).
- **LS-125.** A default is a constant expression evaluated at compile time, once, in the callee's module. There is no shared mutable default.
- **LS-126.** At a call, positional arguments come first, then named arguments as `name = value`. Each parameter receives at most one argument (C2062); unknown names are C2063. Assignment is a statement in this language, so `name = value` inside an argument list is unambiguous.

### 6.4 Multiple returns

**LS-127.** **Specified.** See section 4.7. `return (a, b);` returns a tuple. In an `E!(T1, T2)` function, `return (a, b);` returns success and `return .some_error;` returns an error.

### 6.5 Errors, faults, `try`, and `catch`

**LS-128.** **Specified.**

| | Error | Fault |
|---|---|---|
| Purpose | Expected outcome the caller should handle (bad input, overdraft, or full buffer) | Violation of the integer machine's contract (overflow, bounds, shape, alias, stale handle, or fuel) |
| Spelling | `return .overdrawn;` | Raised by an operation; never constructed by source |
| Handling | `try`, `catch`, `_ =` | Not catchable. Execution stops with a canonical fault record (SPEC-01 IM-118). `expect_fault` tests only observe it (section 12). |
| Representation | A tagged return value (section 4.6); no unwinding | Ends the run or the host call; the context stays faulted until the host program resets it |

- **LS-129.** `try call` evaluates the call. On an error it runs the pending `defer` and `errdefer` statements of the current function (LS-185, LS-189) and returns that error from the current function. The function's error set must hold every value of the callee's set: the same set, or a combined set that includes it (LS-97), and the error's tag is converted through a table fixed at compile time (C2042 otherwise). In a test block, `try` admits an error of any set (LS-315). On success it yields the value. `try` applied to an expression that is not an error union, including a kernel dispatch, is C2043.
- **LS-130.** `call catch value` yields `value` (context-typed to the success type) on any error.
- **LS-131.** `call catch (e) { ... }` binds the error to `e` inside the block. The block must not complete normally: it must end in `return`, `break`, `continue`, or another construct that leaves (C4002). This keeps every expression's value defined without block-expressions.
- **LS-132.** `_ = call;` discards both the value and any error. It is the only way to ignore an error, and it is visible in review.

```ci
I64 v = parse_u(text) catch 0;
I64 w = parse_u(text) catch (e) {
    "bad input: {e}\n";
    return;
};
I64 z = try parse_u(text);           // inside a function whose set contains ParseError
```

### 6.6 Conversions

**LS-133.** **Specified** (spelling). Exact meanings are owned by SPEC-01 IM-50 and IM-68.

| Form | Meaning |
|---|---|
| `x as T` | Checked: the value must be representable in `T`, else `E_NARROW`. Integer to fixed point is exact or faults. Fixed point to integer, and fixed point to a coarser fixed-point type, round `half_even`, then check range. |
| `x as T round mode` | As `x as T`, with the named rounding mode (SPEC-01 IM-42): `x as I32 round floor`. The clause is permitted only where the conversion rounds (C2009 otherwise). |
| `x as% T` | Wrapping: keeps the low `T.bits` bits of the two's complement value. Accepts a `round` clause for fixed-point sources. |
| `x as\| T` | Saturating (Proposed): clamps to `T.min` or `T.max`. |
| `x as? T` | Result-returning: `ArithError!T`, holding `.narrow` where `x as T` faults `E_NARROW` (SPEC-01 IM-188). |

- **LS-134.** There are no implicit conversions between runtime numeric types, including widening. `I32 a; I64 b; b + a` is C2001, with the fix `b + (a as I64)`.
- **LS-135.** `Bool` converts to integers only with `as` (giving 0 or 1). There is no conversion from integers to `Bool` (SPEC-01 IM-10): `n as Bool` is C2056 with the fix `n != 0`. Conditions require `Bool`; `if (n)` is C2002 with the fix `if (n != 0)`.
- **LS-136.** `as` binds tighter than every binary operator (section 7.1): `a as I64 + b` is `(a as I64) + b`.

### 6.7 Calls, evaluation order, and recursion

**Specified** (aligned with SPEC-01 8.1 and 8.2).

- **LS-137.** Operands and arguments are evaluated left to right, each exactly once. Named arguments are evaluated in the order written. Elements of an array literal and arguments of a struct constructor are evaluated left to right.
- **LS-138.** `&&`, `||`, `?:`, and `catch` evaluate only the operands they select.
- **LS-139.** In an assignment `place op= expr`, the place's indices are evaluated and checked before `expr`: `a[-1] = 1 / 0;` faults with `E_BOUNDS`, not `E_DIV_ZERO`.
- **LS-140.** In a destructuring assignment `(p1, p2) = expr;`, every place is evaluated and checked, left to right, before `expr`. With `len(a) == 5`, `(a[10], q) = divmod_floor(1, 0);` faults with `E_BOUNDS`.
- **LS-141.** At a call, all arguments are evaluated, then the entry checks of section 6.2 run in their stated order, then fuel and call depth are charged (below), then the body runs. With `void f[n](inout I64[n] a, in I64[n] b)` and the call `f(x[0..4], x[2..5])`, the shape check fails first: `E_SHAPE` (extent 4 against 3).
- **LS-142.** Function calls always use parentheses. `f;` naming a function without calling it is C4010. There is no parenthesis-free call of the kind HolyC allows [5]. Built-ins that take an operation (`fold_checked(add, ...)`) take an operation name, not a function value (section 6.8).
- **LS-143.** A call statement whose function returns a value other than `void` must use the value or discard it with `_ =` (C4011).

**LS-144.** **Recursion and call depth.** Recursion is permitted outside kernels (Specified). The call-depth limit `D` is a semantic input of each entry and part of its execution identity (SPEC-01 IM-119, IM-159), so every conforming implementation faults at the same call (Specified). It is supplied by the project manifest or the host entry; its default is SPEC-01's (Proposed: 256), and an implementation must accept every `D` up to the minimum supported value of section 20. The call that would make the depth exceed `D` faults `E_DEPTH` (SPEC-01 IM-104, IM-119) before its fuel charge and before its frame is created: the call is not entered and consumes no fuel. An implementation whose native stack cannot hold `D` frames of the compiled program must place frames elsewhere (for example in the frame arena) or refuse the entry with `E_UNSUPPORTED` before execution; it must not crash. Example: `I64 f(I64 n) { if (n == 0) { return 0; } return f(n - 1) + 1; }` called as `f(1_000_000)` with `D = 4096` faults `E_DEPTH` identically on every implementation, at the call that would make 4,097 active calls, counting the entry call.

### 6.8 Built-in operations

**LS-145.** **Specified** (names, argument order, and type rules). Meanings belong to the specification named. This table is the single type-signature table for `cint-core-1`; companion sections cite it (SPEC-01 Open question 5). Notation: `T` is the operand type, unified from the typed arguments (section 4.2 rule 4.7); `R` is a type argument written first; `mode` is a rounding-mode name; `op` is an operation name.

| Type signature | Defined in | Notes |
|---|---|---|
| `len(a)`, `extent(a, d)`, `size(a)`, `lower(a, d)` | SPEC-02, SPEC-03 | `len` on rank above 1 is C2014. `lower` Proposed. |
| `copy(dst, src)`, `fill(dst, v)`, `equal(a, b)` | SPEC-03 | `copy` requires disjoint or identical views (section 4.3). |
| `swap(a, b)` | SPEC-02, SPEC-03 | Exchanges the contents of two owned arrays of identical type; an implementation may exchange handles (no copy). Live views of either are C5011. |
| `transpose(a)`, `reverse(a, d)`, `reshape(a, e1, ...)` | SPEC-02 V-10, V-11 | A view of the same elements, no copy: `transpose` of a rank-2 array or view swaps its dimensions; `reverse` runs dimension `d` backward; `reshape` gives the shape `e1, ...` and faults `E_UNSUPPORTED` when the layout does not permit it (V-11). |
| `abs(x)`, `uabs(x)`, `min(x, y)`, `max(x, y)`, `clamp(x, lo, hi)` | SPEC-01 IM-34, IM-49 | Scalar forms: both arguments scalars of type `T`. |
| `div_trunc(a, b)`, `rem_trunc(a, b)`, `div_euclid(a, b)`, `rem_euclid(a, b)`, `divmod(a, b)` | SPEC-01 IM-36 | `divmod` returns `(T, T)`. |
| `div_round(a, b, mode)` | SPEC-01 IM-44 | Integers. `div_round(7, 2, half_even)` is 4. |
| `muldiv(R, a, b, c, mode)`, `mul_full(a, b)`, `isqrt(x)`, `isqrt_round(x, mode)` | SPEC-01 IM-51 | |
| `add_result`, `sub_result`, `mul_result`, `div_result`, `rem_result`, `shl_result` | SPEC-01 IM-30, IM-188 | Return `ArithError!T`. |
| `rotl(a, k)`, `rotr(a, k)` | SPEC-01 IM-45 | Unsigned only. |
| `mul(a, b, mode)`, `div(a, b, mode)`, `mul_wrap(a, b, mode)`, `mul_sat(a, b, mode)`, `sqrt(a)`, `sqrt(a, mode)` | SPEC-01 5.3 to 5.5 | Fixed point only; `mul` and `div` on integers are C2001 (use `*` and `div_round`). |
| `sum(xs)`, `sum(R, xs)` | SPEC-01 IM-77 | Exact; one checked narrowing to `R` (default: element type). |
| `dot(R, a, b)` | SPEC-01 IM-87 | Rank-1 views of one integer element type; the exact sum of products, then one checked narrowing to `R`; unequal extents fault `E_SHAPE`. |
| `fold_checked(op, init, xs)` | SPEC-01 IM-77 | `op` is `add` or `mul`. |
| `sum_wrap(xs)`, `sum_sat(xs)`, `count(mask)` | SPEC-01 IM-77 | `mask` is a `Bool` array. |
| `min(xs)`, `max(xs)`, `min(init, xs)`, `max(init, xs)` | SPEC-01 IM-77 | A second argument that is an array selects the reduction form; two scalars select the scalar form. |
| `wrap_bits(v, w)` | this document | Proposed (section 4.4). |
| `rescale3(x, k)`, `rescale3_rem(x, k)`, `shl3(x, k)`, `sign(x)`, `tdot(R, w, x)` | SPEC-05 | Spelling Proposed in SPEC-05. `trit(x, i)` (SPEC-05 TR-DIG-2), the conversions of SPEC-05 section 6 (`pack5`, `pack4`, `unpack`, `repack4`, `repack5`, `encode`, `decode_pt5`, `decode_pt4`, `trits`, `from_trits`), and the Kleene connectives `tnot`, `tand`, and `tor` (TR-T1-3, Proposed) are not rows yet, so their names are not reserved. |
| `random(seed, tick, entity, purpose, draw)` | SPEC-02 N-1 | |
| `format(buf, "...")`, `utf8(bytes)` | this document | Section 9.6; `utf8` returns `Utf8Error!Str` derived from `bytes`. |
| `embed("path")` | this document | Section 14 (Proposed). |
| `to_device(v, d)`, `to_host(v)` | SPEC-02 V-12 | |
| `a.alloc(n)`, `a.alloc_result(n)`, `a.child(n)`, `a.reset()`, `p.insert(v)`, `p.insert_result(v)`, `p.remove(h)` | SPEC-03 M-14 to M-20 | Spelling Proposed. `alloc` and `insert` fault when the arena or pool is full; `alloc_result` and `insert_result` return `AllocError!` the same handle instead. |

- **LS-146.** **Operation and mode positions.** The first argument of `fold_checked` is an operation name (`add`, `mul`), and a `mode` position takes a rounding-mode name: `floor`, `ceil`, `trunc`, `away`, `half_even`, `half_away`, `half_trunc`, `half_up`, and `half_down` (SPEC-01 IM-42). These positions accept only names, written bare, and are resolved by position, not by name lookup: a local variable named `floor` does not affect them. A type argument (`R`) is a type name. Anywhere else, a `Round` value is written `.half_even` or `Round.half_even`.
- **LS-147.** `Round` is a built-in enum whose enumerators are the modes above, in that order.
- **LS-148.** The intrinsic names `mul_wide`, `isqrt_floor`, and `div_rne` used in SPEC-08 10.4 are not part of `cint-core-1`; they correspond to `mul_full`, `isqrt`, and `div_round(a, b, half_even)` (correction filed, section 21.2).

## 7. Expressions

### 7.1 Precedence and associativity

**LS-149.** **Specified.** From tightest to loosest:

| Level | Operators | Associativity |
|---|---|---|
| 1 | call `f(...)`, index `a[...]`, member `x.f` | left |
| 2 | unary `-` `-%` `~` `!` `try` | right |
| 3 | `as` `as%` `as\|` `as?` (each with an optional `round` clause) | left |
| 4 | `*` `/` `%` `*%` `*\|` | left |
| 5 | `+` `-` `+%` `-%` `+\|` `-\|` | left |
| 6 | `<<` `>>` `<<%` | left |
| 7 | `&` | left |
| 8 | `^` | left |
| 9 | `\|` | left |
| 10 | `==` `!=` `<` `<=` `>` `>=` | none (section 7.3) |
| 11 | `&&` | left |
| 12 | `\|\|` | left |
| 13 | `?:` | right |
| 14 | `catch` | none |

**LS-150.** Unlike C (ISO/IEC 9899:2018, section 6.5 [1]), the bitwise operators bind tighter than comparisons, so `flags & MASK == 0` means `(flags & MASK) == 0`.

### 7.2 Combinations that require parentheses

**LS-151.** **Specified.** The following combinations are rejected (C2101) with a fix that inserts the parentheses the reader most likely meant, shown both ways when it is ambiguous:

- `&&` and `||` in one expression without parentheses: `a && b || c`.
- A shift operator with an arithmetic operand that is not parenthesized: `1 << n + 1`.
- A bitwise operator (`& | ^`) with an arithmetic operand that is not parenthesized: `a & b + 1`.
- Two different bitwise operators without parentheses: `a & b | c`.
- A unary operator (`-`, `-%`, `~`, `!`) that does not form a negative literal (LS-28), directly followed by a conversion: `-x as% U8` and `-(128) as I8` are rejected regardless of the values involved, with the fixes `(-x) as% U8` and `-(x as% U8)`. A negative literal is one operand: `-1 as% U64` and `- 1 as% U64` are valid (SPEC-01 IM-26).

**LS-152.** Mixing wrapping, saturating, and checked operators of the same level is permitted and evaluates left to right: `a +% b + c` is `(a +% b) + c`.

### 7.3 Comparisons are not chained

**LS-153.** **Specified** (SPEC-01 IM-48). A comparison operand must not itself be an unparenthesized comparison. `a < b < c`, `0 <= i < n`, and `a == b == c` are C2102, with the fix `0 <= i && i < n`. `(a < b) == c` is valid when `c` is `Bool`. Operands of a comparison have the same type; a literal adopts the other operand's type (section 4.2 rule 7).

```ci
if (0 <= i && i < n) { ... }     // bounds test
if (0 <= i < n) { ... }          // C2102: chained comparison; fix shown above
```

### 7.4 Conditional expression

**LS-154.** **Specified.** `c ? x : y` requires `c` to be `Bool` and `x` and `y` to have the same type. Only the selected operand is evaluated.

### 7.5 Arithmetic operators and rounding

**LS-155.** **Specified** (spelling). SPEC-01 sections 4 and 5 own the meanings.

- **LS-156.** `+ - * / %` are checked and fault with `E_OVERFLOW` or `E_DIV_ZERO`. `<< >>` are checked and fault with `E_OVERFLOW` (`<<` only) or `E_SHIFT`. For `+ - * / %` and the bitwise operators, both operands must have the same type. The right operand of `<<`, `<<%`, and `>>` may have any integer type (section 4.2 rule 5).
- **LS-157.** `+% -% *% <<%` wrap. `+| -| *|` saturate. A shift count outside `0 ..= T.bits - 1` faults with `E_SHIFT` in every shift form, and a zero divisor faults `E_DIV_ZERO` in every division form.
- **LS-158.** Integer `/` and `%` are floor division with the remainder taking the divisor's sign (SPEC-01 IM-35): `-7 / 2` is `-4`, `-7 % 2` is `1`. Truncating and Euclidean division are the named functions of section 6.8.
- **LS-159.** Integer division with another rounding is `div_round(a, b, mode)`: `div_round(7, 2, half_even)` is 4.
- **LS-160.** Fixed-point `*` and `/` round half to even. Another mode is requested per operation: `mul(a, b, floor)`, `div(a, b, ceil)`. `%` is not defined on fixed point (C2001).

### 7.6 Indexing and slicing

**LS-161.** **Specified.**

| Expression | Result |
|---|---|
| `a[i]` | Element; checked. |
| `m[i, j]` | Element of a rank-2 array. |
| `p[i]` with `p` of rank above 1 | View of the remaining dimensions (section 4.3). |
| `a[lo..hi]` | View of elements `lo` up to but excluding `hi`. `lo <= hi` and both within `lower ..= lower + len(a)`, else `E_BOUNDS`. |
| `a[lo..=hi]` | View including `hi`. |
| `a[..hi]`, `a[lo..]`, `a[..]` | With a positive step, an omitted first bound is the lower bound and an omitted second bound is the end. |
| `a[lo..hi by 2]` | Strided view. `by` takes a nonzero compile-time constant (C2053). |
| `a[hi..lo by -1]`, `a[hi..=lo by -1]` | With a negative step, the range runs from its first bound downward, excluding the second bound for `..` and including it for `..=`: `a[9..=0 by -1]` is `a[9], a[8], ..., a[0]`. |
| `a[.. by -1]` | With a negative step, an omitted first bound is the last index (`len(a) - 1` plus the lower bound) and an omitted second bound means "through the first index". `a[.. by -1]` is the whole array reversed; for an empty `a` it is empty. |
| `m[.., j]` | Column `j` of a rank-2 array, as a strided rank-1 view. |

A slice that is not valid faults `E_BOUNDS` at its `[` with operation `slice.checked.<E>` and the record of SPEC-01 IM-186. An inclusive slice `a[lo..=hi]` is `a[lo..hi + 1]`, so `a[3..=2]` is empty. With a negative step `k`, `a[f..g by k]` is valid when `lower - 1 <= g <= f <= lower + len(a) - 1`, and `a[f..=g by k]` when `lower <= g <= f + 1 <= lower + len(a)`, so a reversed slice whose bounds meet is empty, not a fault; omitted bounds are as above.

**LS-162.** An array slice has the permission of the expression it is taken from: a slice of an `in` view is read-only. Empty slices, including `a[len(a)..]` and an inclusive or reversed slice whose bounds meet (LS-161), are valid.

## 8. Statements

### 8.1 Blocks, assignment, and increment

**Specified.**

- **LS-163.** Every statement ends with `;` or is a braced construct. There is no empty statement: `if (c);` is C1040.
- **LS-164.** Assignment is a statement, not an expression: `if (x = 5)` is C1041 with the fix `if (x == 5)`.
- **LS-165.** Compound forms exist for every binary arithmetic and bitwise operator, including `+%=`, `+|=`, and `<<%=`. `x += y` means `x = x + y` with `x`'s place evaluated once.
- **LS-166.** `i++;` and `i--;` are statements, checked (`E_OVERFLOW` at the type's limit). They are not expressions: `a[i++]` is C1042.
- **LS-167.** Only calls and dispatches (with `try` where the callee returns an error union), assignments, increments, print statements, `reduce` statements (kernels only), and declarations may stand as simple statements. `x + 1;` is C4012. A type followed by a keyword where a name is expected (`I64 do = 1;`) is also C4012, at the statement's first token.

### 8.2 `if`

**LS-168.** **Specified.** `if (cond) { ... } else if (cond) { ... } else { ... }`. Parentheses around the condition and braces around each body are required (C1043 with a fix); the same rule applies to `while`, `do`, and both `for` forms. An `else if` chain does not increase nesting depth; there is no limit on the number of arms other than total source size. (Note: the legacy native profile has a syntax nesting limit of 128. That `else if` chains consumed this limit in the legacy profile is an observation not yet sourced [citation needed].)

### 8.3 `while` and `do`

**LS-169.** **Specified.** `while (cond) { ... }` and `do { ... } while (cond);`.

### 8.4 `for`, two forms

**Specified.**

**LS-170.** C form:

```ci
for (I64 i = 0; i < n; i++) { ... }
for (I8 i = 0; i <= 127; i++) { ... }   // faults E_OVERFLOW when i++ passes 127; never loops forever
```

The initializer declares a variable scoped to the loop (or is a simple statement), the condition is `Bool`, and the update is a simple statement. The variable is mutable.

**LS-171.** Range form:

```ci
for i in 0..n { ... }            // 0, 1, ..., n-1
for i in 1..=n { ... }           // 1, ..., n
for i in 0..n by 4 { ... }       // 0, 4, 8, ... below n
for i in 10..0 by -1 { ... }     // 10, 9, ..., 1
for x in values { ... }          // each element, in index order
for i, x in values { ... }       // index and element
```

- **LS-172.** The bounds are evaluated once, left to right, before the first iteration. The bounds and the loop variable have type `I64` (SPEC-01 IM-7); a literal bound is typed `I64`, and a bound of a narrower type is converted explicitly, `for i in (lo as I64)..(hi as I64)`. A bound of another type is C2001.
- **LS-173.** The loop variable is read-only inside the body (C2052). To change the iteration, use the C form.
- **LS-174.** If the range is empty (`lo >= hi` for a positive step), the body does not run.
- **LS-175.** Iteration never computes a value past the last element: `for i in 0..=I64.max` and `for i in (I8.min as I64)..=(I8.max as I64)` run to completion without overflow. `by` must be a nonzero constant (C2053).
- **LS-176.** `for x in view` iterates a rank-1 view in index order. `x` is a copy of the element read at the start of that iteration. In `for i, x in view`, `i` runs from the view's lower bound.

### 8.5 `switch`

**Specified.**

```ci
switch (code) {
    case 0:
        "ok\n";
    case 1..=4, 9:           // 1, 2, 3, 4, and 9
        "warning {code}\n";
    case 5..=8:
        "error {code}\n";
        fallthrough;
    case 10:
        "logged\n";
    default:
        "unknown {code}\n";
}
```

- **LS-177.** The scrutinee (the value the `switch` tests) is an integer, enum, `Bool`, or error value. Case items are constants or inclusive constant ranges `lo..=hi`. A half-open range in a case label is C4022, with the fixes `case 1..=4:` and `case 1..=5:`. This closes the trap in which `case 'a'..'z':` would silently omit `'z'`.
- **LS-178.** A case body runs to the next `case`, `default`, or the closing brace. There is no implicit fallthrough. `fallthrough;` as the last statement of a body continues into the next body.
- **LS-179.** A case clause with an empty body is C4023, with the fix of merging its items into the next clause: `case 'a'..='z': case 'A'..='Z': return TK_IDENT;` becomes `case 'a'..='z', 'A'..='Z': return TK_IDENT;`. A C programmer's empty-label idiom therefore never compiles to "do nothing."
- **LS-180.** `break;` inside a switch exits the switch, as in C. To leave an enclosing loop from inside a switch, use a labeled `break` (section 8.6).
- **LS-181.** Overlapping or duplicate case items are C4021.
- **LS-182.** A switch must be exhaustive: either it has `default`, or its cases cover every value of the scrutinee's type (every enumerator; every value of an error set; both `Bool` values; the full range of an integer type) (C4020). Sections 4.5 and 4.6 (C2054) ensure that an enum value is always an enumerator and an error-set value always one of the set's values.
- **LS-183.** `default` must be last. Each case body is its own scope.

### 8.6 `break`, `continue`, and labels

**LS-184.** **Specified.** A loop may be labeled: `rows: for i in 0..h { ... }`. `break;` and `continue;` apply to the innermost loop (or, for `break`, the innermost switch). `break rows;` and `continue rows;` name a loop. There is no `goto`.

### 8.7 `defer` and `errdefer`

**Specified.**

```ci
void step(inout Arena(I64) arena) {
    Arena(I64) scratch = arena.child(65_536);   // child arena of 65,536 elements, SPEC-03 M-20 (spelling Proposed)
    defer scratch.reset();
    ...
}
```

- **LS-185.** `defer stmt;` or `defer { ... }` schedules the statement to run when the enclosing block exits by falling off its end, `return`, `break`, `continue`, or `try` propagation. Deferred statements run in reverse order of declaration.
- **LS-186.** Variables in the deferred statement are read when it runs, not when it is declared.
- **LS-187.** A deferred statement, including the statement of an `errdefer`, must not contain `return`, `break`, `continue`, `try`, or `defer` that would leave it (C4030).
- **LS-316.** (Specified.) `return expr;` evaluates `expr`, then runs the pending deferred statements, then returns the value of `expr`. A deferred statement cannot change the returned value, although it reads variables when it runs (LS-186).
- **LS-317.** (Specified.) Running a deferred statement charges no fuel of its own; the calls, loop iterations, and other charge points inside it charge as anywhere else (SPEC-01 IM-139). A fault in a deferred statement is an ordinary fault at its own position, with the call stack of the function that declared it.
- **LS-318.** (Specified.) A `defer` in a loop body is scheduled on each iteration and runs when that iteration's block exits, including by `continue` or `break`.
- **LS-319.** (Specified.) A deferred statement declared inside a `catch (e) { ... }` block runs when that block exits.
- **LS-320.** (Specified.) A deferred statement may consume an error union with `catch` or `_ =` (LS-95); `try` stays excluded (LS-187).
- **LS-321.** (Specified.) In a script, a `defer` at top level runs when the script's implicit entry ends normally, by falling off its end or by `return;` (LS-190).
- **LS-188.** Deferred statements do not run after a fault: a fault stops execution, and its fault record describes the state at the fault. Python's `with` runs `__exit__` when an exception leaves its suite ([4], section 8.5). CINT's `defer` does not run after a fault, so host cleanup for a faulted entry belongs to the host program (SPEC-01 IM-118, SPEC-03 A-7b). This is final for `cint-core-1`: no construct contains a fault inside its own context.

**LS-189.** **Specified.** `errdefer stmt;` runs only when the function exits by returning an error, by `return .name;` or by `try` propagation, interleaved with deferred statements in reverse order of declaration (LS-185). Like `defer`, it does not run after a fault (LS-188). No form binds the error, and `errdefer` is reserved as a keyword (LS-18).

**LS-322.** **Specified.** An `errdefer` is admitted only where an error can leave its block: in a function whose result type is an error union, and in a test block, where an error leaves by `try` (LS-315) and the `errdefer` runs when it does. Elsewhere an `errdefer` could never run, and it is C4031: in a function or kernel whose result is not an error union, at the top level of a script, whose implicit entry returns no error union, and inside a deferred statement, which no error can leave (LS-187).

### 8.8 `return`

**LS-190.** **Specified.** `return;` in `void` and `E!void` functions; `return expr;` otherwise. In a script (section 10), `return;` at top level ends the script. In a test block, `return;` ends the test as if its body had completed (section 12).

### 8.9 `assert` and `static_assert`

**LS-191.** **Specified.** `assert(cond);` or `assert(cond, "message {x}");`. If `cond` is a comparison, a failure reports both operand values exactly, in the manner of pytest's assertion rewriting [9]. If line 57 of the tutorial (section 19) expected 1 instead of 0, `cint test` would report:

```text
assertion failed at ledger.ci:57:12 in test "interest ties round to even"
   |
57 |     assert(daily_interest(1_825_000, 1) == 1);
   |            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   = left:  0
   = right: 1
```

Outside tests, a failed `assert` faults `E_ASSERT` (code 13, SPEC-01 IM-104) with operation `assert.checked.bool`, at the first character of the condition. The record carries the two operands of the condition when it is a comparison, and none otherwise; the message is not evaluated. A condition that is not `Bool` is C2002.

**LS-192.** `static_assert(cond);` or `static_assert(cond, "message");` is evaluated at compile time; failure is C6010.

## 9. The print statement and exact formatting

### 9.1 Print statement

**LS-193.** **Specified.** A statement consisting of one or more adjacent string literals followed by `;` writes the formatted bytes to the program's output channel:

```ci
"hello\n";
"x={x} y={y}\n";
"long lines can be split " "across adjacent literals\n";
```

- **LS-194.** No newline is added. Output bytes are exactly the UTF-8 bytes produced; LF is never translated to CR LF.
- **LS-195.** Formatting is independent of locale, host operating system, and backend. Digits are ASCII, and the minus sign is `-` (U+002D).
- **LS-196.** Printing is an effect: in replay it is recorded per SPEC-06. Kernels must not print (C5020).
- **LS-197.** Holes are evaluated left to right, each once. If a hole faults, nothing from that statement is written.

### 9.2 Holes

**LS-198.** **Specified.**

```text
{expr}            value with the default rendering for its type
{expr=}           the source text of expr, "=", then the value
{expr!conv}       a conversion applied before rendering
{expr:spec}       a format specification
{expr=!conv:spec} all three, in this order
```

- **LS-199.** The hole expression is the longest prefix that parses as an expression. It must not contain string literals or braces, and a conditional expression must be parenthesized: `{(ok ? a : b)}` (C1035). A conversion not listed in LS-201 is also C1035.
- **LS-200.** For `{expr=}`, the printed name is the expression's source text exactly as written, including spaces.

**LS-201.** Conversions:

| Conversion | Applies to | Meaning |
|---|---|---|
| `!bits` | integers, fixed point | The two's complement storage bits as an unsigned value of the same width. With `x`, `o`, or `b`, zero-padded to the full width. |
| `!raw` | fixed point | The underlying storage integer (`x.raw`). |
| `!ratio` | fixed point | `raw/2^f` exactly, for example `6442450944/2^32`. (Proposed) |
| `!lanes(T)` | integers | The value split into lanes of type `T`, lane 0 (least significant) first, rendered as an array. |

### 9.3 Format specification

**LS-202.** **Specified.** The syntax is adapted from Python's Format Specification Mini-Language [3]. The `t` kind and the `/` scale suffix are CINT additions, and the rules below govern wherever the two differ.

```text
spec      = [[fill] align] [sign] ["#"] ["0"] [width] [group] ["." precision] [kind] ["/" scale]
align     = "<" left | ">" right | "^" center
sign      = "-" (default: only negatives) | "+" (always) | " " (space for non-negative)
"#"       = radix prefix: 0x 0o 0b
"0"       = zero-pad after the sign and prefix, to width
width     = minimum width in Unicode scalar values
group     = "_" or "," every 3 digits for decimal; "_" every 4 digits for x, X, o, b
precision = digits after the point, for fixed point and scaled integers
kind      = d decimal | x X hex | o octal | b binary | t balanced ternary | c character | s string
scale     = a power of ten, 10 or greater: print an integer divided by it, exactly
```

The digits of `width`, `precision`, and `scale` are the ASCII digits `0` to `9`. Any other character there is C1037, including a decimal digit of another script that Python's mini-language accepts: `{x:>٣}` (U+0663) is C1037, where Python 3.11 formats `format(1, '>٣')` as `'  1'`.

Rules:

1. **LS-203.** Integers in `x`, `o`, and `b` are printed by mathematical value with a leading `-` for negatives: `{-255:x}` is `-ff`. The storage bit pattern is printed only with `!bits`. C's reinterpretation of negative values is never implicit.
2. **LS-204.** Radix prefixes are always lowercase (`0x`, `0o`, `0b`); `X` affects only the hex digits, so `{255:#X}` is `0xFF`.
3. **LS-205.** `{x:t}` prints the minimal balanced-ternary literal of the value, prefix included, as SPEC-05 TR-FMT-2 specifies: digits `N`, `0`, `P`, most significant first, no leading `0` digit, with the sign carried by the digits. `{8:t}` is `0tP0N`, `{-8:t}` is `0tN0P`, and `{0:t}` is `0t0`. The output is always a valid literal of the same value.
4. **LS-206.** `/scale` prints an integer `v` as the exact decimal `v / scale` with exactly `log10(scale)` fractional digits: `{1234:/100}` is `12.34`, `{-5:/100}` is `-0.05`. Precision is not permitted with scale (C1036). This is how scaled-integer money and fixed-decimal units print without fixed-point types.
5. **LS-207.** Fixed-point values print by default as the shortest exact decimal with at least one fractional digit. The value of a `Q<i>.<f>` number is `raw / 2^f`, which always has a terminating decimal expansion of at most `f` fractional digits, so the default is always exact.
6. **LS-208.** `.precision` on a fixed-point value rounds the exact value to that many digits, half to even, and prints exactly that many digits. If the rounded value is zero, no minus sign is printed.
7. **LS-209.** `c` prints an integer as the character with that code point; a value that is not a Unicode scalar value faults with `E_NARROW`.
8. **LS-210.** There is no exponent or scientific notation, so a value is never shown in an approximate form such as `9.22e18`.
9. **LS-211.** A width smaller than the rendering never truncates.
10. **LS-212.** Alignment, fill, and width apply to every rendering, including enum names, error values, `Bool`, and `Str`. They apply to the whole rendering of a scalar and to each element of an array.
11. **LS-213.** A spec that does not apply to the value's type is C1037. In particular: `.3` on an integer; `x` on a `Str`; `#` with `t` (the prefix is always printed); a sign flag (`+`, ` `) or a group (`_`, `,`) with `t`; `,` with `x`, `X`, `o`, or `b`; `#`, `0`, group, or precision with `c` or `s`. The combinations these rules do not define are also C1037: a width with no alignment and no `0` flag; `0` together with an explicit alignment or a group; `#` with decimal; a scale together with a kind other than `d`, `#`, a group, or a conversion; a sign flag with `c`; `0` with `t`; `!bits` with `t` or `c`; `!lanes(T)` with a signed `T` or with a lane width that does not divide the value's width; and any conversion applied to `Bool`.

### 9.4 Default rendering by type

**LS-214.** **Specified** (except where marked).

| Type | Default rendering |
|---|---|
| Integer, any width | Exact decimal, `-` for negatives. `I1024` values print in full. |
| Fixed point | Shortest exact decimal, at least one fractional digit (`3.0`, `-0.25`). |
| `Bool` | `true` or `false` |
| Enum | Enumerator name (`burn`). With `#`, qualified (`Mode.burn`). |
| Error value | Qualified (`LedgerError.overdrawn`) |
| `Str` | Its bytes |
| `T1` | `-1`, `0`, `1`; with `t`, `0tN`, `0t0`, `0tP` |
| `T27` | Exact decimal; with `t`, the minimal literal |
| Array (any rank) | `[a, b, c]`, nested for higher rank, every element, each with the hole's spec applied. No truncation; print a slice to shorten. |
| Struct | `Body(id=7, mass=1.5, pos_mm=[0, 0, 0], flags=0)`. Proposed. |

### 9.5 Examples

**LS-215.** **Specified.**

| Hole | Value | Output |
|---|---|---|
| `{n}` | `I64` -42 | `-42` |
| `{n:+}` | `I64` 0 | `+0` |
| `{n:08}` | `I64` -42 | `-0000042` |
| `{n:>6}` | `I64` 42 | `    42` |
| `{n:*^7}` | `I64` 42 | `**42***` |
| `{n:_}` | `I64` 1234567 | `1_234_567` |
| `{n:,}` | `I64` 1234567 | `1,234,567` |
| `{n:x}` | `I64` 255 | `ff` |
| `{n:#X}` | `I64` 255 | `0xFF` |
| `{n:x}` | `I64` -255 | `-ff` |
| `{n:#_x}` | `U32` 0xDEADBEEF | `0xdead_beef` |
| `{n!bits:x}` | `I8` -1 | `ff` |
| `{n!bits:#b}` | `I8` -128 | `0b10000000` |
| `{n!bits:x}` | `I64` -1 | `ffffffffffffffff` |
| `{n:t}` | `I64` 8 | `0tP0N` |
| `{n:t}` | `I64` 0 | `0t0` |
| `{n:#t}` | `I64` 8 | C1037 |
| `{n:c}` | `I64` 233 | `é` |
| `{c:/100}` | `I64` 123456 | `1234.56` |
| `{c:/100}` | `I64` -5 | `-0.05` |
| `{c:/100}` | `I64` `I64.min` | `-92233720368547758.08` |
| `{q}` | `Q32.32` 1.5 | `1.5` |
| `{q}` | `Q32.32` 3 | `3.0` |
| `{q}` | `Q32.32` raw 1 | `0.00000000023283064365386962890625` |
| `{q:.2}` | `Q32.32` 0.125 | `0.12` (tie, rounded to even) |
| `{q:.2}` | `Q32.32` 0.375 | `0.38` (tie, rounded to even) |
| `{q:.1}` | `Q32.32` -0.03125 | `0.0` (no negative zero) |
| `{q:.3}` | `Q16.16` raw 1 | `0.000` |
| `{q!raw}` | `Q32.32` 1.5 | `6442450944` |
| `{q!ratio}` | `Q32.32` 1.5 | `6442450944/2^32` |
| `{w!lanes(U8):02x}` | `U32` 0x12345678 | `[78, 56, 34, 12]` |
| `{a}` | `I32[3]` | `[1, -2, 3]` |
| `{m}` | `I32[2, 2]` | `[[1, 2], [3, 4]]` |
| `{a:+}` | `I32[3]` | `[+1, -2, +3]` |
| `{k:<8}` | `Kind.deposit` | `deposit ` |
| `{ok}` | `Bool` | `true` |
| `{x=}` | `I64` 5 | `x=5` |
| `{a[i] + 1 =}` | `I64` 7 | `a[i] + 1 =7` |
| `{{` and `}}` | | `{` and `}` |

### 9.6 Formatting into a buffer

**LS-216.** **Specified.** `format(buf, "...")` writes the formatted bytes into a writable `U8` view and returns `FormatError!Str`, a `Str` derived from `buf` (section 5.1) covering the written prefix. If the output does not fit, it writes the longest prefix that fits without splitting a UTF-8 sequence, leaves the remaining buffer bytes unchanged, and returns `FormatError.capacity`. Both outcomes are deterministic. There is no allocating string formatting in `cint-core-1`.

```ci
U8[64] buf;
Str label = format(buf, "body {id:04}") catch "body ????";
```

**LS-217.** `label` may be used while `buf` is live; it cannot be returned from the function that declares `buf` (C2051).

## 10. Programs, scripts, and the workbench

**Specified.**

- **LS-218.** A **script** is a module that contains top-level statements. Its top-level statements run in source order when the script is run (`cint run file.ci`) or loaded into the workbench, as a notebook cell [6] or a HolyC file [5] runs.
- **LS-219.** In a script, type, struct, enum, error, arena, pool, function, kernel, schedule, and constant declarations are visible throughout the module regardless of order (section 5.3); an arena or pool is created with the context (SPEC-03 M-20). Top-level variable declarations are locals of the script's implicit entry: each is visible from its declaration to the end of the script and is never visible inside a function, kernel, or test. A function that needs such a value takes it as a parameter. This preserves the rule of section 5.1 that no read is uninitialized:

```ci
void show() { "{balance}\n"; }   // C3004: balance is a script local, not visible here
show();
I64 balance = 5;
```

- **LS-220.** An **importable module** contains only declarations, imports, tests, and `static_assert`. A script cannot be imported (C3010); move reusable declarations into an importable module.
- **LS-221.** A program may instead define `void main()` or `E!void main()` in a module without top-level statements. A module with both top-level statements and `main` is C3011. If `main` returns an error, the run's result is that error value, which is distinct from a fault. The entry completes: the context is not faulted, and at the C ABI the status is `CINT_OK` with `is_error` set in the result (SPEC-03 A-18). `cint run` exits with status 6 and names the set, the value, and the tag on stderr (SPEC-06 3.2, 3.4a).
- **LS-222.** Top-level statements and `main` run with the same typed core, the same arithmetic, and the same faults as compiled code. The workbench has no separate interpreter semantics.
- **LS-223.** There is no postfix or alternate grammar. Workbench input is `cint-core-1` source; workbench commands (`:checkpoint`, `:inspect` and so on) are a separate command language owned by SPEC-06.

**LS-312.** **Specified.** A diagnostic that depends on whether a module is a script (C3004, C3010, C3011, and the module-level initializer error C6004 of LS-113) carries a note naming the first top-level statement, in source order, that makes the module a script, or stating that the module has none, and one help line. In the machine form (section 17.5) the note carries its file, line, and column. The code and the position of the diagnostic do not change (section 17.2), and message text is not compared (SPEC-09 9.2). A `test` block does not make a module a script.

```text
error[C3004]: `balance` is a script local; it is not visible in a function or test
  --> ledger.ci:2:36 in read_balance
   |
 2 | export I64 read_balance() { return balance; }
   |                                    ^^^^^^^ a local of the script, declared at ledger.ci:1:5
   |
   = note: this module is a script because of the top-level statement at ledger.ci:3:1: `"ready\n";`
   = help: move the top-level statements into `void main() { ... }`; `balance` is then a module-level variable that `read_balance` can read
```

C3010 and C3011 take the same note; the help of C3011 is "move the top-level statements into `main`". C6004 takes "= note: this file is a module, not a script, because every top-level item is a declaration (LS-218)". Examples written for readers follow one convention: an example that declares a function together with a variable used by more than one statement or function uses `void main() { ... }` from its first version.

**LS-224.** **Proposed.** In the workbench, an input that is a single expression prints its value and type, for example `7 / 2` shows `3 : I64`. Redefining a name in a live session creates a new code revision; checkpoints retain the revision (SPEC-06). Between entries, a live context can change to the new revision: a binding whose module, qualified name, and type are unchanged keeps its value, a new binding starts at its initializer (LS-113), and a binding that is retyped or removed is dropped (SPEC-03 H-19b). A variable declared at the prompt is a module-level variable of the session's own module, kept in checkpoints and visible to functions declared later; only a declaration entry makes a revision of that module, and a script loaded into a session runs as one entry, as `cint run` runs it, so LS-219 is unchanged (SPEC-06 5.2).

## 11. Modules

**Specified.**

```ci
// file: geo/vec.ci       (module geo.vec)
export struct V3 { I64 x; I64 y; I64 z; }
export I64 dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
I64 helper() { return 0; }          // private to geo.vec

// file: orbit.ci
import geo.vec;                     // use as vec.V3, vec.dot3
import geo.vec as v;                // or as v.V3
import geo.vec.{V3, dot3};          // or unqualified, selected names only
```

- **LS-225.** One file is one module. A module's name is its path relative to a project source root, with `/` replaced by `.` and `.ci` removed. The source roots are declared in the project manifest owned by `cint build`. Each segment of a module path, without the final `.ci`, is an identifier (LS-15), so the name and the path map one to one; any other module path is C3030 in `cint build` (SPEC-09 CINTC-12). `a/b.ci` and `a.b.ci` therefore cannot both name `a.b`.
- **LS-226.** `import` declarations come before any other item. Imports are not transitive.
- **LS-227.** `export` makes a declaration visible to importers. Everything else is private to its module. Exported structs export all their fields. (Proposed.) A workbench entry may assign an exported module-level variable of a module the session has loaded (SPEC-06 5.2); whether an importing module may assign one is left open here.
- **LS-228.** One definition per name: within a module, every name (type, function, constant, variable, enum, error set, kernel, import alias) is declared exactly once (C3001). There is no overloading. Imported names are qualified unless selected, and a selected name that collides with a local or another selected name is C3002.
- **LS-229.** There are no headers, prototypes, or include guards. A module is compiled from its own source and the exported interfaces of its imports.
- **LS-230.** Import cycles are C3003.
- **LS-231.** Exposure to C, Python, or Fortran is not implied by `export`; it is requested with `@abi(c)` and defined by SPEC-03. Only types that SPEC-03's element encoding can describe may appear in an `@abi(c)` type signature (C5050). SPEC-03 A-13 describes every `Q<i>.<f>` by its storage tag and `frac_bits`, so every fixed-point type is eligible. SPEC-03 A-25 lists the forms that no `@abi(c)` type signature holds at T3: `Str`, `PT5`, `PT4`, `Arena`, `Pool`, `Handle`, `Round`, an array result, and an array extent outside the `shape_expr` of SPEC-02 K-1. Each is C5050 with `@abi(c)`, and under the interim rule of SPEC-03 A-12 an export with one of them gets no wrapper.

## 12. Tests

**Specified.**

```ci
test "floor division of negatives" {
    assert(-7 / 2 == -4);
    assert(-7 % 2 == 1);
}

test "narrowing faults" expect_fault E_NARROW {
    I64 big = 300;
    I8 small = big as I8;
}
```

- **LS-232.** `test "name" { ... }` may appear at the top level of any module, including scripts. Tests are compiled and run only by `cint test`. They may use the module's private names; they cannot see a script's top-level variables (section 10).
- **LS-233.** Test names are plain strings (no holes) and must be unique within a module (C3020).
- **LS-234.** Each test runs in a fresh machine context with fresh module state; tests cannot observe one another. Tests run in source order within a module and modules in path byte order (SPEC-06 3.5), so test output is deterministic.
- **LS-235.** A test without `expect_fault` passes if its body completes, including by `return;` (LS-190). It fails on a failed `assert`, on any fault, or when an error leaves it (LS-315).
- **LS-315.** (Specified.) `try` is admitted in a test block, for an error of any set. An error that leaves the test by `try` propagation fails it, and the failure report names the error as `cint run` names an error result of `main` (SPEC-06 3.4a, 3.5). There is no `expect_error` header. A test that expects a specific error binds it with `catch` and asserts on it; with `parse_u` of section 4.6:

```ci
test "a letter is a bad digit" {
    I64 v = parse_u("1x") catch (e) {
        assert(e == .bad_digit);
        return;
    };
    assert(false);                       // parse_u returned a value
}

test "two digits parse" {
    I64 v = try parse_u("42");           // an error here fails the test
    assert(v == 42);
}
```

- **LS-236.** `test "name" expect_fault E_NAME { ... }` (the form of SPEC-06 3.5) applies to the whole test. The test passes only if its body faults and the canonical fault has code `E_NAME`. The fault ends the test, as it ends any execution (SPEC-01 IM-118); nothing after it runs, deferred statements do not run, and the test's context is discarded. The test fails if the body completes, an error leaves it, or it faults with a different code. A fault is never caught or resumed, in tests or elsewhere. To check several faults, write several tests.
- **LS-237.** `expect_fault` is not a statement and is not permitted outside a test header (C3021).
- **LS-238.** Failure reports use the format of section 17.
- **LS-239.** Top-level statements of a script do not run during `cint test`.

**LS-240.** **Proposed.** `expect_fault E_NAME at 12` additionally requires the fault position's line (SPEC-06 3.5).

## 13. Attributes and kernel surface

### 13.1 Attribute syntax

**LS-241.** **Specified.** An attribute is `@name` or `@name(args)` placed before a declaration or field. Unknown attributes are C5030. The language-defined annotations are:

| Annotation | Form | Applies to | Status |
|---|---|---|---|
| `kernel` | keyword | declaration | Specified |
| `in`, `out`, `inout` | keywords | parameters, view variables | Specified |
| `over`, `reduce`, `in_place` | keywords | kernel declaration, kernel body, dispatch | Specified (spelling owned here; SPEC-02 O-8) |
| `schedule` | keyword | separate declaration | Proposed |
| `@soa` | attribute | struct | Proposed |
| `@packed` | attribute | struct | Specified |
| `@align(n)` | attribute | struct, field | Proposed |
| `@abi(c)` | attribute | function, kernel, struct | SPEC-03 |
| `@pure` | attribute | function | Proposed: asserts kernel admissibility (section 13.2); checked |
| `@deprecated("text")` | attribute | any declaration | Proposed |

**LS-242.** Packed trit arrays are types (`PT5[...]`, `PT4[...]`, SPEC-05), not attributes. `kernel`, the parameter modes, `over`, `reduce`, `in_place`, and `schedule` are keywords rather than attributes because they change what a construct is, not how it is stored.

### 13.2 Kernels

**LS-243.** **Specified.** This section is the surface of the single-program, multiple-data (SPMD) model of SPEC-02 sections 4 to 8, which owns execution meaning, publication, aliasing, the canonical fault order, and backend obligations. A kernel body is written for one work-item: one execution of the kernel body for one index tuple. This follows the SPMD model of ISPC [25].

```ci
// stencil.ci: one step of a double-buffered integer diffusion stencil (1-D).
kernel diffuse[n](in I32[n] u, out I32[n] next) over [i: n] {
    I32 r = u[i];
    if (i > 0 && i < n - 1) {
        I32 lap = u[i - 1] - 2 * u[i] + u[i + 1];
        r = u[i] + div_round(lap, 4, half_even);
    }
    next[i] = r;
}

// Driver: double buffering by swap, no copy.
I32[1024] field;
I32[1024] scratch;
field[512] = 65_536;
for t in 0..240 {
    diffuse(field, scratch);       // staged dispatch; publishes scratch on success
    swap(field, scratch);          // exchanges the two buffers; no element copy
}
```

The two-dimensional form of the same stencil, with an exact heat total, is SPEC-02 section 15.

Surface rules checked by the compiler:

1. **LS-244.** **Declaration.** `kernel name[s1, ...](params) [over [v1: s1, ...]] { body }`. The bracketed size symbols are `I64` values bound at dispatch entry (SPEC-02 K-5, K-6). Each parameter has an explicit mode. An optional `where` clause after the `over` clause states requirements on size symbols and is checked at dispatch entry (SPEC-02 K-1, F-5 check 5). Kernels have no result type.
2. **LS-245.** **Parameters.** `in` arrays are read-only and may overlap each other. `out` arrays are written, never read. `inout` arrays are read (pre-dispatch values, SPEC-02 P-4) and written only within the work-item's owned block (SPEC-02 K-2). A scalar `in` is uniform. A scalar `out` is the target of exactly one `reduce` statement (rule 6). Every `out` and `inout` argument must not overlap any other argument (C5010 when provable, `E_ALIAS` at dispatch entry otherwise; SPEC-02 A-2 to A-8).
3. **LS-246.** **Element types.** Array parameters have element types admitted by SPEC-02 V-1: integer types, fixed-point types, `T1`, `T27`, and packed trit types. Scalar parameters have integer, fixed-point, `T1`, or `T27` type. Other types (`Bool`, `Str`, structs, enums) are C5028.
4. **LS-247.** **Iteration space.** With an `over` clause, the body runs once per index tuple of the clause; the `over` variables are `I64`, read-only, and varying. Without an `over` clause the body is in element form (SPEC-02 K-12): only whole-array assignments of elementwise expressions, such as `next = old + increment;`.
5. **LS-248.** **Write rule** (SPEC-02 K-8, K-10, K-10a). Every write to an array `out` or `inout` parameter has the `over` variables, in order, as its leading indices; any further indices address the work-item's owned block and are bounds-checked. Writes may occur under `if` and inside loops; the last write in evaluation order is the staged value. Every element of each array `out`'s owned block must be written on every path that completes, as established by the coverage patterns of SPEC-02 K-10a. A write outside the owned block, or coverage the compiler cannot establish by those patterns, is C5031. There is no implicit zero fill.
6. **LS-249.** **Reductions** (SPEC-02 section 8). `reduce target = op(...);` where `target` is a scalar `out` and `op(...)` is one of the in-kernel forms of SPEC-02 8.2: `sum(e)`, `fold_checked(add, init, e)`, `sum_wrap(e)`, `sum_sat(e)`, `min(e)`, `max(e)`, or `count(c)`. It may not appear inside a loop; it may appear under `if`.
7. **LS-250.** **Body restrictions** (SPEC-02 K-11). A kernel body must not print, use `defer`, `try`, or error unions, dispatch a kernel, call a function that is not kernel-admissible, read or write module-level variables, or recurse (C5020 to C5026). A function is kernel-admissible when it obeys the same restrictions; `@pure` asserts it and the compiler checks the assertion. Private arrays follow section 5.1 (C5029).
8. **LS-251.** **Dispatch.** A dispatch is a statement in sequential code, written like a call: `diffuse(field, scratch);`. It has no value and no error union; `try diffuse(...)` is C2043. Its failure is a fault carrying the canonical logical address (section 17.4). Outputs are staged and published only if the whole dispatch succeeds (SPEC-02 P-1); an `inout` argument has copy-in, copy-out meaning (P-4).
9. **LS-252.** **In-place dispatch.** `in_place cool(t, 1);` dispatches with direct writes to `inout` arguments (SPEC-02 P-6 to P-9). It is admitted only if every read of each `inout` parameter uses the work-item's own index tuple (C5032 otherwise). The in-place failure contract is SPEC-02 P-9; a dispatch without `in_place` is always staged.
10. **LS-253.** **Views across a dispatch.** A view variable derived from an `out` or `inout` argument that is live across a dispatch of that argument, or across a `swap` of its array, is C5011 (the rule of SPEC-03 M-23 applied to dispatch). Therefore no program can observe whether the runtime published by renaming the output buffer or by copying out of staging (SPEC-02 P-5), and both strategies give the same faults.

```ci
in I32[_] row = scratch[0..8];
diffuse(field, scratch);           // C5011: row views scratch and is used below
"{row[0]}\n";
```

**LS-254.** **Publication cost (Specified).** A staged dispatch costs, per `out` or `inout` argument, either one fresh allocation with no copy (rename, for runtime-owned buffers) or one staging allocation plus one full copy (copy-out, for borrowed buffers with fixed addresses).

**LS-255.** **Publication strategy (Proposed, SPEC-02 P-5).** Which of the two strategies a runtime chooses is Proposed in SPEC-02 P-5. For the driver above, the arrays are runtime-owned, so with renaming each step allocates one `I32[1024]` output and copies nothing; `swap` exchanges handles. `cint explain` reports the strategy chosen for each dispatch site.

**LS-256.** **Index representation on narrow targets (Specified).** Shape symbols, `over` variables, and index arithmetic are `I64` (SPEC-01 IM-7). A backend may represent an `I64` value in fewer bits only when the value's range is proven, for example from extent bounds checked at dispatch entry (such as `n <= 2^31 - 1`); such a representation preserves every value and is not narrowing in the sense of SPEC-01 IM-8 or SPEC-02 B-1. Where the range is not proven, the backend implements full `I64` or refuses with `E_UNSUPPORTED`. `cint explain` lists each proven range and the entry check that establishes it.

**LS-257.** **Proposed.** Whole-array expressions in sequential functions, for equal shapes and scalar broadcast: `next = old + increment;` meaning elementwise checked addition.

### 13.3 Schedules

**LS-258.** **Proposed.** A schedule is a separate declaration that changes how a kernel runs and never what it computes (SPEC-02 H-1 to H-5), following the separation of algorithm and schedule in Halide [26].

```ci
schedule diffuse {
    device(gpu);          // requirement
    tile(i, 256);         // request; i is the over variable
    vectorize(i, 8);      // request
}
```

- **LS-259.** Directive names, kinds, and arguments are defined by SPEC-02 H-3. Variables named by a directive are the kernel's `over` variables; any other name is a compile error (C5041).
- **LS-260.** A request (`tile`, `vectorize`, `parallel`, `unroll`, or `workgroup`) may be declined by a backend. A declined request is not an error; `cint explain` lists every directive with its outcome and reason (SPEC-02 H-4). A backend never declines silently.
- **LS-261.** A requirement (`device(...)`) that is known to be unmet at compile time is C5040, reported with `E_UNSUPPORTED` as the reason. A requirement whose satisfaction depends on the device found at run time (for example, the Vulkan `shaderInt64` feature [27], SPEC-02 B-14) is checked at dispatch entry and faults `E_UNSUPPORTED` (SPEC-02 F-5, check 4). In neither case does a backend fall back silently or narrow.
- **LS-262.** Removing a schedule must not change any output or fault of a conforming program. The differential debugger checks this.

### 13.4 `@soa`

**Proposed.**

```ci
@soa struct Particle { I64 x; I64 v; }

Particle[1024] ps;          // stored as two contiguous I64[1024] columns
ps[3].x = 5;                // element syntax is unchanged
in I64[_] xs = ps.x;        // a contiguous view of one column
```

**LS-263.** `@soa` changes the storage of every array of the struct type to one array per field. Element access and the meaning of every program are unchanged; only layout, and therefore view strides and the host-boundary descriptor, differ. A single struct value is stored normally.

## 14. Compile-time execution

**Specified.**

- **LS-264.** Compile-time evaluation happens for every maximal constant expression, wherever it appears (SPEC-01 IM-23), and in constant initializers, array extents, default arguments, case items, `static_assert`, enum values, and attribute arguments, which must be constant (C6004 otherwise). A constant operand below the right side of `&&` or `||`, or below an arm of `?:`, of a non-constant expression is not evaluated at compile time; it runs, and faults, only if reached. A constant expression that its statement always evaluates is still C6001 when it faults, even in a statement that a run may never reach; C# likewise reports a constant subexpression whose evaluation would throw as a compile-time error [28].
- **LS-265.** It uses the same typed core as run time: the same operator meanings, the same faults, and the same formatting (SPEC-01 IM-103). Literal-only expressions are typed first and evaluated with the run-time operators (section 4.2), so compile-time and run-time evaluation never differ. A fault during compile-time evaluation is a compile error (C6001) that names the fault and its exact operands.
- **LS-266.** A function may be called at compile time if it is const-eligible: it performs no printing, reads no clock, environment variable, file, or host service, and calls only const-eligible functions. Eligibility is inferred.
- **LS-267.** Compile-time evaluation is charged in `fuel-v1` units (SPEC-01 IM-139: one unit per call and per loop iteration). Exceeding the module's compile-time budget is C6002 reporting `E_FUEL`. The default budget is Proposed as 1,000,000,000 units per module, the value of SPEC-06 4.3, adjustable in the project manifest; it is part of the build identity. Because the unit is defined, whether a program compiles does not depend on the implementation.
- **LS-268.** There is no access to clocks, environment variables, the network, or files not declared in the project manifest. This keeps compilation a pure function of declared inputs, as a content-addressed build cache requires.

**LS-269.** **Proposed.** `embed("tables/coeffs.bin")` yields a compile-time `in U8[_]` constant with the file's bytes. The path must be declared in the project manifest (C6003 otherwise), and its content hash is part of the build identity.

**LS-270.** **Out of scope for compile time.** Code generation, reflection over arbitrary declarations, and compile-time types as values (beyond `T.min`, `T.max`, `T.bits`, `T.bytes`, and `offset`).

## 15. Deliberately absent

**LS-271.** **Specified.** The following are absent from `cint-core-1` by design. Each absence is a decision, not an omission to be filled silently by an implementation.

| Absent | Reason | Instead |
|---|---|---|
| Floating-point types, literals and operations | Results would depend on evaluation order, contraction (ISO/IEC 9899:2018, section 6.5 [1]), and library implementations | Fixed point, scaled integers, wide integers |
| Raw pointers and pointer arithmetic | Unbounded aliasing and no bounds | Views with shape and permissions; generation-checked handles |
| Implicit conversions, integer promotion | C's promotion rules (ISO/IEC 9899:2018, section 6.3.1.1 [1]) are a source of silent errors [29] | `as`, `as%`, context-typed literals |
| Preprocessor and macros | Text substitution hides meaning from tools and diagnostics | `const`, `import`, compile-time functions |
| Undefined behavior within the defined scope | Section 16 | Checked operators and faults |
| Uninitialized variables | Undefined reads | Required initializers, zero-filled aggregates |
| `goto`, `setjmp` | Unstructured control flow | Labeled `break` and `continue`, `defer` |
| Exceptions | Hidden control flow; faults are not recoverable | Error unions for errors; faults stop |
| Implicit switch fallthrough | A known C weakness (CWE-484 [30]) | `fallthrough;` |
| Chained comparisons | Two specifications disagreed; the explicit form is one token longer | `0 <= i && i < n` |
| Operator overloading, function overloading | One definition per name; readable diffs | Distinct function names |
| Untagged unions, type punning | Reinterpretation without a defined result | `!bits`, `.bits[...]`, `.lanes(T)`, `as%` |
| Variadic functions | Unchecked arguments | Default and named arguments, views |
| Postfix or parenthesis-free call grammar | One grammar for programs and the workbench | Parenthesized calls; workbench commands are separate |
| Negative indexing | Silent wraparound to the end | Explicit `len(a) - 1`; reversed views |
| Implicit allocation (string concatenation, growable arrays in the core, hidden temporaries for overlapping copies) | Hidden cost and failure | `format(buf, ...)`; library containers; explicit temporaries (section 4.3) |
| Shared-memory threads in source | Data races | Kernels with exclusive outputs |

## 16. Scope of defined behavior

**Specified.**

**LS-272.** Within the defined scope, a `cint-core-1` program that compiles has exactly one outcome for given semantic inputs (section 1): either it completes with specified outputs and output bytes, or it stops with one canonical fault, after a specified prefix of effects. There is no unspecified evaluation order, uninitialized read, out-of-bounds access, signed-overflow undefined behavior, dangling view or `Str` (section 5.1), or data race in source-level code. Call depth and frame storage are bounded by semantic inputs and exhaust with a fault (sections 5.1 and 6.7), never with a native crash.

**LS-273.** The defined scope covers: every construct in this document; compile-time evaluation; print output; faults and their records. It relies on the companion specifications for arithmetic, views, kernels, and ternary operations. Where a companion section currently disagrees with this document, the disagreement is listed in section 21.2 and the claim of this section holds only once it is resolved.

**LS-274.** The defined scope does not cover, and this document makes no claim about:

- code reached through the host boundary (C, Python, Fortran, and operating-system services), or external modification of borrowed buffers, which SPEC-03 classifies;
- defects in an implementation, its generated C, or its host C compiler;
- hardware faults;
- exhaustion of host resources other than those bounded by semantic inputs, where the implementation cannot detect it and report it as a fault.

**LS-275.** No safety-standard compliance is claimed (for example, with MISRA C [31] or the SEI CERT C Coding Standard [32]) for this language or its implementations. Such a claim would need a documented assessment against specific editions and rules.

## 17. Diagnostics

### 17.1 Text format

**LS-276.** **Specified.** Compile errors and run-time faults share one layout:

```text
<severity>[<code>]: <one-line message>
  --> <file>:<line>:<column>[ in <function or kernel>]
   |
NN | <source line, verbatim>
   | <spaces><carets> <operation or note>
   |
   = <label>: <exact value>
   = help: <explicit fix>
```

For a run-time fault, every tool prints the layout of the LS-284 example: the `<label>` lines are the canonical fields of the SPEC-01 IM-149 record under their field names, in the order of that example, followed by `fuel-consumed` and `state`, which are not part of the record, and then the lines a tool adds that are not part of the record (SPEC-06 8.4). ` in <function or kernel>` and the `help` lines are optional additions, and `cint run` prints neither.

- **LS-277.** `severity` is `error` (compile time), `fault` (run time), or `assertion failed`. `cint-core-1` defines no warnings: every construct is either valid or rejected. (Proposed; lint tools are separate.)
- **LS-278.** For a run-time fault, `line:column` is the canonical position of SPEC-01 IM-106 and SPEC-02 F-8: the operator token of the faulting operation (for an index, the `[`; for a call, the callee name; for a dispatch entry fault, the dispatch site). Carets mark that token. The extent of the whole offending expression is non-canonical display data, carried as `span` in the machine form (section 17.5). For a compile error, the position is the first character of the offending construct and carets underline it; where the offending construct has two readings, the position table of section 17.2 names the token. Every coded diagnostic has a position: C1001, C3030, and a C9001 that concerns a whole file are reported at `<path>:1:1`. A fault raised by `i++`, `i--`, or a compound assignment is at its operator token (`++`, `+=`), and an `E_FUEL` fault at the token of its charge point (SPEC-01 IM-141).
- **LS-279.** If the source line contains tabs, the caret line reproduces them at the same positions so that carets align in any tab width.
- **LS-280.** Every value is printed exactly, in decimal, with its type. When a fault concerns a range, the report prints the bound that the result crossed as `limit`. A value is never abbreviated, rounded, or shown in scientific notation. Fixed-point operands show both the exact decimal value and the raw integer.
- **LS-281.** **Redaction.** Faults inside the key agent are reported with operation and position only and no operand values (SPEC-07 SEC-SD-1). (Proposed, with SPEC-07 SEC-SD-2:) an operand of a `secret`-qualified value renders as `<secret T>`, for example `<secret I64>`, in the text form, the machine form, and the inspector.
- **LS-282.** Compile errors are reported in order of (file, line, column, code). At most 100 errors per module are reported, then a count of the rest. The output is deterministic. An implementation that stops at the first error (the seed compiler, SPEC-09 SEED-07) reports the least diagnostic in (line, column, code) order among the errors of the earliest phase that finds one. A compile-error conformance case holds one error, and its `.expect` records the first diagnostic in this order.

### 17.2 Compile-time codes

**LS-283.** **Specified** (the scheme, and the 17 numbers that frozen `.expect` files cite); **Proposed** (the other numbers). Compile diagnostics carry numeric `C` codes in the categories below. This is the single compile-diagnostic code scheme of `cint-core-1`; SPEC-06 8.5 (`C_` names), SPEC-01 IM-23 and its CIF-1 fixtures (`compile_error: E_NARROW`), and SPEC-03 M-23 (compile error "with code `E_ALIAS`") are to cite it (section 21.2). A compile-time fault (C6001) carries the `E_` code as a field, so a fixture that expects `E_NARROW` at compile time expects `C2003` (constant out of range) or `C6001` with code `E_NARROW` (fault during evaluation). The numbered table below is the single list of codes. `cint_ref` and the seed compiler report the same number for the same error. The 17 codes that 33 frozen `.expect` files cite (C1002, C1003, C1010, C1020, C1021, C1022, C1032, C1040, C2001, C2002, C2003, C2020, C2052, C4012, C4022, C4023, C6001) are Specified numbers, so renumbering one needs a new language profile (SPEC-09 CONF-02).

| Range | Category | Examples in this document |
|---|---|---|
| C1xxx | Lexical and syntactic | C1003 forbidden invisible or bidirectional character, C1022 leading zero, C1033 hole outside a format context, C1041 assignment in condition |
| C2xxx | Types, conversions, constants, and views | C2001 implicit conversion, C2003 literal does not fit, C2006 inexact fraction, C2051 view escapes, C2054 enum or error set without zero value needs initializer, C2101 parentheses required, C2102 chained comparison |
| C3xxx | Names, modules, and tests | C3001 duplicate name, C3003 import cycle, C3004 script local used in a function, C3010 importing a script |
| C4xxx | Control flow | C4001 missing return, C4002 catch block completes, C4020 non-exhaustive switch, C4022 half-open case range, C4023 empty case body |
| C5xxx | Parameter modes, kernels, schedules, attributes, and ABI | C5010 provable alias, C5011 view live across dispatch, C5012 argument written through module state, C5031 kernel write rule, C5040 unmet schedule requirement, C5050 type not ABI-eligible |
| C6xxx | Compile-time evaluation | C6001 fault during constant evaluation, C6002 compile-time fuel |
| C9xxx | Implementation limits | Section 20 |

**LS-313.** **Specified** for the 17 frozen codes named in LS-283, **Proposed** for the rest. The codes, with the position of each code whose offending construct has two readings. A code with no position entry is reported at the first character of the offending construct (LS-278).

| Code | Meaning | Position |
|---|---|---|
| C1001 | `.cint` file (legacy profile) | `<path>:1:1` |
| C1002 | byte-order mark, invalid UTF-8, or NUL (LS-4) | |
| C1003 | forbidden invisible or bidirectional character (LS-6) | the character |
| C1004 | control character or lone CR | |
| C1005 | unterminated block comment | |
| C1010 | reserved type name `[IUQT][0-9]+` | |
| C1011 | unknown `E_` name | |
| C1012 | identifier over 255 bytes | |
| C1013 | non-ASCII character outside a literal or comment (LS-5) | |
| C1020 | uppercase radix prefix | |
| C1021 | misplaced digit separator | |
| C1022 | leading zero | |
| C1023 | literal over 4,096 bits of magnitude (LS-31) | the literal's first character |
| C1024 | radix prefix with no digits, or a digit outside the radix | |
| C1030 | empty or multi-character character literal | |
| C1031 | escape that is not a Unicode scalar value | |
| C1032 | unknown escape | the backslash |
| C1033, C1034 | hole outside a format context; line break in a string literal | |
| C1035 | invalid hole expression, or an unknown hole conversion | |
| C1036, C1037 | precision with scale; a spec that does not apply or is not defined (LS-213) | |
| C1038 | unterminated string or character literal | |
| C1039 | lone `}` in a string | |
| C1040 | empty statement | |
| C1041 | assignment in a condition | the assignment's target |
| C1042 | `++` or `--` used as an expression | |
| C1043 | missing parentheses or braces in `if`, `while`, `do`, or either `for` form | the first character of the unbraced body |
| C1050 | syntax error: an expected token or construct | |
| C2001 | type mismatch (`C-TYPE-MISMATCH`), only for true mismatches | |
| C2002 | condition not `Bool`, including the operand of `!`, `assert`, and `static_assert` | |
| C2003 | literal does not fit its type | the literal, inside any parentheses; the `-` of a negative literal |
| C2004 to C2007, C2009 | fraction, fixed-point type, enum literal, and rounding rules | |
| C2008 | no explicit conversion between the two types (`as`, `as%`) | |
| C2010 to C2014 | array rules | |
| C2015 | negative constant extent (LS-62) | |
| C2020 | struct field not supplied | |
| C2022 | wrong number of arguments or fields | |
| C2030, C2031, C2040 to C2043, C2051, C2053, C2054 | enums, error sets and unions, tuples, `try`, view lifetime, `by`, a zero-filled enum or error-set value (LS-91, LS-314) | |
| C2044 | error value name declared by several member sets of a combined set (LS-97) | |
| C2050 | uninitialized scalar or `Str` variable | the declared name |
| C2052 | loop variable assigned | |
| C2055 | struct that contains itself | |
| C2056 | integer used as `Bool` | the conversion's operand |
| C2058 | not an assignable place, or a constant | |
| C2060 to C2063 | parameter assigned; default order; duplicate or unknown named argument | |
| C2064 to C2067 | `inout` scalar parameter; size parameter bound by no view; return value missing or extra; `inout` argument not writable | |
| C2069 | positional argument after a named one | |
| C2101 | parentheses required (LS-151) | the first character of the expression that needs them |
| C2102 | chained comparison | the first operand of the chain |
| C2103 | operator or built-in not defined for the operand type, including `Bool` arithmetic and struct comparison (SPEC-05 X-10) | |
| C3001 | duplicate name, including a duplicate field or a built-in name (LS-112) | the declared name |
| C3002 | selected import name collides | |
| C3003 | import cycle | |
| C3004 | script local used in a function or test (LS-219, with the LS-312 note) | |
| C3005 to C3008 | undefined name or member; unknown type; wrong kind of name; name not exported | |
| C3009 | module file unreadable, the root file included | |
| C3010, C3011 | importing a script; a script with `main` (with the LS-312 note) | |
| C3012 | `import` after other items | |
| C3020, C3021 | test and `expect_fault` placement | |
| C3030 | module path or build manifest path refused (`C-PATH`, SPEC-09 CINTC-12) | `<path>:1:1` |
| C4001 | missing return | the function's name in its declaration |
| C4002 | `catch` block completes | |
| C4010, C4011 | function named without a call; result unused | |
| C4012 | statement form not permitted, including a keyword where a name is expected (LS-167) | |
| C4013 | `break` or `continue` outside a loop | |
| C4014 | misplaced `fallthrough` | |
| C4020 | non-exhaustive `switch` | the `switch` keyword |
| C4021 | overlapping case items | the later item |
| C4022, C4023 | half-open case range; empty case body | |
| C4024 | empty case range | |
| C4025, C4026 | `default` not last or repeated; scrutinee type not permitted | |
| C4030 | `defer` leaves its statement | |
| C4031 | `errdefer` where no error can leave (LS-322) | |
| C4040 | recursion in `cint-boot-1` (`C-RECURSION`, SPEC-09 EMIT-30) | |
| C5001 | `out` in a function | |
| C5010 to C5012 | alias and view rules | |
| C5020 to C5026 | kernel body restrictions, in LS-250 order: print, `defer`, `try` or an error union, dispatch, a call that is not admissible, a module-level variable, recursion | |
| C5028 to C5032, C5040, C5041 | kernel types, attributes, the write rule, schedules | |
| C5042 | non-positive directive size | |
| C5050 | type not ABI-eligible | |
| C5051 | public symbol cannot be formed: two exports map to one name, or `__` in a `cx` or `cm` symbol (`C-SYMBOL-COLLISION`, SPEC-09 EMIT-22) | |
| C5060 | impure watch or inspect expression (SPEC-06 17.2 item 18) | |
| C6001 | fault during constant evaluation | the faulting operator |
| C6002 | compile-time fuel | |
| C6003 | `embed` path not declared in the project manifest (LS-269) | |
| C6004 | constant expression required, including a module-level initializer (LS-113, with the LS-312 note) | |
| C6005 | constant depends on itself | |
| C6006 | constant arguments violate a built-in precondition (`clamp` with `lo > hi`; a negative arena or pool capacity, SPEC-03 M-20) | |
| C6010 | `static_assert` failed | |
| C9001 | capacity or output size exceeded (`C-LIMIT`) | `<path>:1:1` when it concerns a whole file |
| C9002 | generated symbol over 247 bytes (SPEC-09 EMIT-22) | |
| C9003 | local array over the implementation's frame limit | |
| C9004 | nesting beyond the implementation's limit (section 20) | the construct that exceeds it |
| C9100 | outside `cint-boot-1`: the seed's refusal of a case outside its subset (SPEC-09 CONF-14 category 4, outside the subset) | |
| C9101 | the specification leaves this undecided; the message names the Open item | |
| C9102 | not yet supported by this implementation; the message names the limit | |

When the clashing name of C3001 is a built-in, the message says so: "`count` is a built-in function (LS-145) and cannot be declared again", with "= help: choose another name, for example `ticks`".

Examples:

```text
error[C2001]: expected I64, found I32
  --> orbit.ci:14:24 in total_mass
   |
14 |     I64 total = base + extra;
   |                        ^^^^^ I32
   |
   = note: cint-core-1 has no implicit conversions, including widening
   = help: convert explicitly: `base + (extra as I64)`
```

```text
error[C2003]: constant 200 does not fit I8
  --> sensor.ci:3:12
   |
 3 |     I8 x = 200;
   |            ^^^ I8 is -128 ..= 127
   |
   = help: use a wider type: `I16 x = 200;`
```

### 17.3 Run-time faults

**LS-284.** **Specified.** A fault report names the fault, the operation, every operand exactly (subject to section 17.1, redaction), the canonical source position, and the call path. Operation identifiers are those of SPEC-01 IM-130, lowercase, of the form `<op>.<form>.<type>[.<type2>][.<mode>]`: for example `add.checked.i64`, `shl.wrap.u32`, `as.checked.i64.i8`, `div.checked.i128`, `mul.checked.q16_16.half_even`. SPEC-01 owns the list.

Taking the tutorial of section 19 and changing the last book entry to `Entry(.deposit, I64.max)`, `balance + e.cents` in `apply` faults `E_OVERFLOW` at `ledger.ci:26:28` with the operation `add.checked.i64`, the operands `I64 84960` and `I64 9223372036854775807`, the exact result 9223372036854860767, the limit `I64 9223372036854775807`, and one call-site position, `ledger.ci:48:15` at the top level. A suggestion (LS-292) may add that balances that can exceed `I64` belong in `I128`, which changes the types of `balance`, `Entry.cents`, and the result of `apply`; it is not applied automatically.

Proposed. The text form is the one `cint run` prints: the fault line, the position, and the source excerpt, then one line per canonical field of SPEC-01 IM-106 under its field name (`operation`, `operand-count`, one `operand` line per operand, `exact`, `limit`, `revision`, `source-map`, `address`, and `stack-depth`), then `fuel-consumed` and `state`, which are not part of the fault record. A fault inside a called function adds one `called from` line per call-site position after `stack-depth`, outermost first. `examples/hello_overflow.ci` faults at line 4 with this report:

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> hello_overflow.ci:4:16
   |
 4 | I64 next = big + 1;
   |                ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   = revision: 7130e3356ca58e02a16a0ee10db7b4941fd9f745230bd7147b7830092c0f68be
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: the program stopped; stdout holds what it wrote before the fault
```

**LS-285.** Fault names and their minimum report fields (SPEC-06 8.2 gives the full per-code fields). The text form prints them as the canonical fields of the LS-284 example: operands as `operand` lines, the exact mathematical result as `exact`, and the type range as the bound the result crossed, `limit` (Proposed).

| Fault | Fields beyond operation and position |
|---|---|
| `E_OVERFLOW` | operands, exact mathematical result, and type range |
| `E_DIV_ZERO` | dividend, divisor |
| `E_BOUNDS` | index or range, dimension, and extent (or lower and upper bound) |
| `E_SHAPE` | both shapes and the size parameter that disagreed |
| `E_SHIFT` | value, count, and type width; the record's limit is `I64(w - 1)`, and the text form shows the allowed range `0..w-1` (SPEC-01 IM-45) |
| `E_NARROW` | value, source type, target type, and its range |
| `E_ALIAS` | both parameters, their buffer identity, and the overlapping element range |
| `E_STALE_HANDLE` | handle, its generation, and the arena's current generation |
| `E_FUEL` | budget, consumed, and the charge point (call, loop iteration, or dispatch) |
| `E_DEPTH` | depth limit, the call that would exceed it |
| `E_UNSUPPORTED` | operation, backend, and missing capability |
| `E_DOMAIN` (SPEC-01 IM-104) | operand |
| `E_ASSERT` (SPEC-01 IM-104) | the two operands of a comparison condition, or none |

**LS-286.** The canonical fault record is never truncated to fit a fixed-size host structure. Operands can exceed 1,024 bits (an `I1024` product needs up to 2,047; a `sum` over `I1024` needs up to 1,087) and an `E_ALIAS` fault record holds two descriptors. Carrying the full canonical fault record across the C ABI is SPEC-03's obligation (section 21.2).

### 17.4 Kernel faults

**LS-287.** **Specified.** A fault inside a kernel additionally reports the canonical logical address: dispatch number, work-item index, and step ordinal within that work-item. The work-item index is the row-major linear `I64` index (SPEC-01 IM-112, SPEC-02 K-9) and is canonical; the index tuple is printed beside it as a non-canonical aid. The reported fault is the canonical one under the ordering of SPEC-02 F-3, not whichever lane failed first on the hardware.

**LS-288.** **Specified** (address values). Dispatch numbers count from 0 within each entry (SPEC-01 IM-112, SPEC-02 F-1). Step ordinals count every counted operation of SPEC-01 IM-113 from 0 (SPEC-02 F-6). The example below follows both.

Setup: the kernel of section 13.2 (lines numbered from the listing's first line, so the `I32 lap` declaration is line 5), `n = 1024`, every `u` element 0 except `u[1022] = -700` and `u[1023] = 2147483000`, dispatched once in a fresh context. Work-items 0 to 1021 complete (item 1021 computes `lap = -700`). Work-item 1023 is a boundary cell. At work-item 1022, `u[1021] - 2 * u[1022]` is `0 - (-1400) = 1400`, and `1400 + u[1023]` is `2147484400`, which exceeds `I32.max`. The checked `+` at line 5, column 39, faults. Step ordinals under SPEC-01 IM-113: `u[i]` on line 3 is step 0; `n - 1` on line 4 is step 1; on line 5, `i - 1` is step 2, `u[i - 1]` step 3, `u[i]` step 4, `2 * u[i]` step 5, `-` step 6, `i + 1` step 7, `u[i + 1]` step 8 and the faulting `+` step 9.

In the layout of LS-276 and LS-284, with the digests shown as placeholders. The dispatch is the entry, so the stack is empty, and the fuel consumed is the dispatch's charge of 1,024 units (SPEC-01 IM-139):

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> stencil.ci:5:39 in kernel diffuse
   |
 5 |         I32 lap = u[i - 1] - 2 * u[i] + u[i + 1];
   |                                       ^ add.checked.i32
   |
   = operation: add.checked.i32
   = operand-count: 2
   = operand: I32 1400
   = operand: I32 2147483000
   = exact:   2147484400
   = limit:   I32 2147483647
   = revision: <revision identity>
   = source-map: <source-map digest>
   = address: stencil.diffuse, dispatch 0, phase 1, work-item 1022, step 9
   = stack-depth: 0
   = fuel-consumed: 1024 (not part of the fault record)
   = state: nothing published (outputs of a failed dispatch are not published)
   = help: compute the Laplacian in I64 and narrow once; the narrowing can still
           fault E_NARROW. Replace lines 5 and 6 with:
               I64 lap = (u[i - 1] as I64) - 2 * (u[i] as I64) + (u[i + 1] as I64);
               r = ((u[i] as I64) + div_round(lap, 4, half_even)) as I32;
```

With the edit applied, work-item 1022 computes `lap = 2147484400`, `div_round(lap, 4, half_even) = 536871100`, and `r = 536870400`, which fits `I32`.

### 17.5 Machine-readable form

**LS-289.** **Specified.** `--json` writes one canonical JSON object [33] per diagnostic and per line, in the order of the text form, using the canonical JSON rules of SPEC-06 12.1 (keys sorted by UTF-8 bytes, no insignificant whitespace, machine values as typed exact-decimal strings) and the `CINT-FAULT-1` format owned by SPEC-06 8.4. The key that names the format is `schema`, as SPEC-06 section 15 requires. This document fixes only that the machine form carries every field of the text form, including `span` and the non-canonical index tuple, and that canonical fields equal those of the SPEC-01 IM-106 fault record. Fields that are not part of the record are keys of the object's `noncanonical` object, among them `fuel_consumed` and the index tuple, and the `state` line has no key (SPEC-06 8.4 and 15); how the machine form carries `span` and the `function` of a stack entry is open (OQ-171). The 17.3 fault:

```json
{"code":"E_OVERFLOW","column":28,"exact":{"t":"Z","v":"9223372036854860767"},"file":"ledger.ci","function":"apply","help":[{"text":"if balances can exceed I64, hold them in I128: change the types of `balance`, `Entry.cents`, and the result of `apply` (not applied automatically)"}],"limit":{"t":"I64","v":"9223372036854775807"},"line":26,"operands":[{"t":"I64","v":"84960"},{"t":"I64","v":"9223372036854775807"}],"operation":"add.checked.i64","schema":"CINT-FAULT-1","severity":"fault","span":{"end_column":37,"start_column":20},"stack":[{"column":15,"file":"ledger.ci","function":"(top level)","line":48}]}
```

**LS-290.** `span.end_column` is exclusive. Kernel faults add `dispatch`, `step`, `work_item` (the linear index, canonical), and `work_item_tuple` (non-canonical). The canonical three are keys of the `address` object that `cint run` writes, beside `kernel` and `phase`, and `work_item_tuple` is a key of the `noncanonical` object (SPEC-06 15).

### 17.6 Rules for suggested fixes

**Specified** (aligned with SPEC-06 8.5).

1. **LS-291.** A fix must be an explicit source edit whose meaning is stated. It must never be "add a cast" without saying which cast and what it does.
2. **LS-292.** A diagnostic offers at most one suggestion. A suggestion never replaces a checked operator or conversion with a wrapping or saturating one, and never suggests `as%` to silence a range error. It may suggest an explicit checked conversion, a wider type, a named operation with a wide intermediate (`muldiv`, `mul_full`), or a declared range.
3. **LS-293.** A fix must not change the meaning of any other part of the program, and the program with the fix applied must compile. A fix that introduces a new checked operation says which fault it can raise.
4. **LS-294.** Machine-applicable fixes carry an `edit` in the JSON form. Tools may apply them only on request. A fix that changes declared types in several places (as in section 17.3) is text only.

## 18. Grammar

**LS-295.** **Specified**, except for productions marked Proposed in the sections above, which are included so the grammar is complete. Notation: `=` defines, `;` ends a rule, `|` separates alternatives, `[ x ]` is optional, `{ x }` is zero or more, `( )` groups, quoted text is a terminal, and `(* *)` is a comment. Lexical rules are in capitals; the lexer applies maximal munch and skips whitespace and comments between tokens. Static rules stated in the text (types, mixing restrictions, exhaustiveness, kernel write rules) are not repeated here.

### 18.1 Lexical grammar

**LS-296.** The lexical productions:

```ebnf
letter      = "A".."Z" | "a".."z" | "_" ;
digit       = "0".."9" ;
nonzero     = "1".."9" ;
hexdigit    = digit | "a".."f" | "A".."F" ;
sep         = "_" ;                                   (* only between two digits *)

IDENT       = letter { letter | digit } ;             (* excluding keywords and [IUQT][0-9]+ *)
DEC_INT     = "0" | nonzero { [ sep ] digit } ;
HEX_INT     = "0x" hexdigit { [ sep ] hexdigit } ;
OCT_INT     = "0o" ( "0".."7" ) { [ sep ] ( "0".."7" ) } ;
BIN_INT     = "0b" ( "0" | "1" ) { [ sep ] ( "0" | "1" ) } ;
TERN_INT    = "0t" ( "N" | "0" | "P" ) { [ sep ] ( "N" | "0" | "P" ) } ;
DEC_FRAC    = DEC_INT "." digit { [ sep ] digit } ;
HEX_FRAC    = HEX_INT "." hexdigit { [ sep ] hexdigit } ;
CHAR        = "'" ( char_char | escape ) "'" ;
STRING      = '"' { str_char | escape | "{{" | "}}" | hole } '"' ;
BYTES       = 'b"' { byte_char | escape | "{{" | "}}" } '"' ;
escape      = "\\" ( "\\" | '"' | "'" | "n" | "r" | "t" | "0"
                   | "x" hexdigit hexdigit
                   | "u{" hexdigit [ hexdigit [ hexdigit [ hexdigit [ hexdigit [ hexdigit ] ] ] ] ] "}" ) ;
INT_TYPE    = "I8" | "I16" | "I32" | "I64" | "U8" | "U16" | "U32" | "U64"
            | "I128" | "I256" | "I512" | "I1024" ;
FIXED_TYPE  = "Q" nonzero { digit } "." ( "0" | nonzero { digit } ) ;
LINE_COMMENT  = "//" { any_char_except_line_end } ;
BLOCK_COMMENT = "/*" { BLOCK_COMMENT | any_char } "*/" ;   (* nesting: Proposed *)
```

**LS-297.** Fault names are `IDENT`s checked against SPEC-01 IM-104 (section 3.4). `round` is matched as `IDENT` with the spelling `round` in the positions of 18.6.

### 18.2 Format holes

**LS-298.** The productions of format holes:

```ebnf
hole        = "{" hole_expr [ "=" ] [ "!" conversion ] [ ":" spec ] "}" ;
hole_expr   = expr ;              (* no string literals, no braces, ternary parenthesized *)
conversion  = "bits" | "raw" | "ratio" | "lanes" "(" INT_TYPE ")" ;
spec        = [ [ fill ] align ] [ sign ] [ "#" ] [ "0" ] [ width ] [ group ]
              [ "." precision ] [ kind ] [ "/" scale ] ;
fill        = any_scalar_except_braces ;
align       = "<" | ">" | "^" ;
sign        = "+" | "-" | " " ;
width       = digit { digit } ;
group       = "_" | "," ;
precision   = digit { digit } ;
kind        = "d" | "x" | "X" | "o" | "b" | "t" | "c" | "s" ;
scale       = "1" "0" { "0" } ;
```

### 18.3 Modules and declarations

**LS-299.** The productions of modules and declarations:

```ebnf
module        = [ profile_decl ] { import_decl } { top_item } ;
profile_decl  = "profile" STRING ";" ;
import_decl   = "import" module_path
                [ "as" IDENT | "." "{" IDENT { "," IDENT } [ "," ] "}" ] ";" ;
module_path   = IDENT { "." IDENT } ;

top_item      = { attribute } [ "export" ] declaration
              | test_decl
              | static_assert
              | statement ;                         (* scripts only, section 10 *)

attribute     = "@" IDENT [ "(" [ attr_arg { "," attr_arg } ] ")" ] ;
attr_arg      = expr | IDENT "=" expr ;

declaration   = const_decl | var_decl | func_decl | kernel_decl | schedule_decl
              | struct_decl | enum_decl | error_decl | type_decl ;

const_decl    = "const" type IDENT "=" expr ";" ;                (* every constant is typed, SPEC-01 IM-24 *)
var_decl      = type IDENT [ "=" expr ] ";"
              | "var" IDENT "=" expr ";"                          (* Proposed *)
              | "var" "(" bind { "," bind } ")" "=" expr ";"      (* Proposed *)
              | "(" typed_bind "," typed_bind { "," typed_bind } ")" "=" expr ";"
              | ( "in" | "inout" ) type IDENT "=" expr ";" ;
bind          = IDENT | "_" ;
typed_bind    = type IDENT | "_" ;

func_decl     = result_type IDENT [ size_params ] "(" [ params ] ")" [ where ] block ;
size_params   = "[" IDENT { "," IDENT } "]" ;
params        = param { "," param } [ "," ] ;
param         = [ "in" | "inout" ] type IDENT [ "=" expr ] ;   (* "out": C5001 *)
where         = "where" expr ;                                  (* Proposed *)
result_type   = "void" | type | ( "in" | "inout" ) type
              | qualified "!" ( "void" | type ) ;

kernel_decl   = "kernel" IDENT size_params "(" kparam { "," kparam } [ "," ] ")"
                [ over_clause ] [ kernel_where ] block ;
kparam        = ( "in" | "out" | "inout" ) type IDENT ;
over_clause   = "over" "[" IDENT ":" extent { "," IDENT ":" extent } "]" ;   (* SPEC-02 K-1 *)
kernel_where  = "where" expr { "," expr } ;                       (* size-symbol constraints, SPEC-02 K-1 *)

schedule_decl = "schedule" IDENT [ "for" IDENT ] "{" { IDENT "(" [ attr_arg { "," attr_arg } ] ")" ";" } "}" ;

struct_decl   = "struct" IDENT "{" { field } "}" ;
field         = { attribute } type IDENT [ ":" expr ] [ "=" expr ] ";" ;
enum_decl     = "enum" IDENT ":" INT_TYPE "{" enumerator { "," enumerator } [ "," ] "}" ;
enumerator    = IDENT [ "=" expr ] ;
error_decl    = "error" IDENT [ ":" INT_TYPE ]
                ( "{" IDENT { "," IDENT } [ "," ] "}"
                | "=" qualified { "|" qualified } ";" ) ;     (* combined set, LS-97 *)
type_decl     = "type" IDENT "=" [ "distinct" ] type ";" ;   (* "distinct": Proposed *)

test_decl     = "test" STRING [ "expect_fault" IDENT [ "at" DEC_INT ] ] block ;
                (* IDENT is a fault name; "at": Proposed *)
static_assert = "static_assert" "(" expr [ "," STRING ] ")" ";" ;
```

### 18.4 Types

**LS-300.** The productions of types:

```ebnf
type          = elem_type [ shape ] | tuple_type ;
elem_type     = INT_TYPE | FIXED_TYPE | "Bool" | "Str" | "T1" | "T27"
              | "PT5" | "PT4" | "Arena"                         (* PT5, PT4, Arena: Proposed *)
              | ( "Pool" | "Handle" ) "(" type ")"              (* Proposed *)
              | qualified ;
shape         = "[" [ extent { "," extent } ] "]" ;
extent        = "_" | expr [ ( ".." | "..=" ) expr ] ;          (* ranges: lower bounds, Proposed *)
tuple_type    = "(" type "," type { "," type } ")" ;
qualified     = IDENT { "." IDENT } ;
```

### 18.5 Statements

**LS-301.** The productions of statements:

```ebnf
block         = "{" { statement } "}" ;
statement     = block
              | const_decl | var_decl
              | simple_stmt ";"
              | print_stmt
              | if_stmt
              | [ IDENT ":" ] loop_stmt
              | switch_stmt
              | jump_stmt
              | defer_stmt
              | assert_stmt
              | reduce_stmt                         (* kernels only *)
              | static_assert ;

simple_stmt   = place assign_op expr
              | place ( "++" | "--" )
              | "(" bind_place { "," bind_place } ")" "=" expr
              | "_" "=" expr
              | [ "try" ] postfix_expr              (* must be a call or a dispatch *)
              | "in_place" postfix_expr ;           (* must be a kernel dispatch *)
place         = IDENT { "." IDENT | "[" index_list "]" | "." IDENT "(" type ")" } ;
bind_place    = place | "_" ;
assign_op     = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "|=" | "^="
              | "+%=" | "-%=" | "*%=" | "<<%=" | "+|=" | "-|=" | "*|=" ;

print_stmt    = STRING { STRING } ";" ;

if_stmt       = "if" "(" expr ")" block [ "else" ( if_stmt | block ) ] ;
loop_stmt     = "while" "(" expr ")" block
              | "do" block "while" "(" expr ")" ";"
              | "for" "(" for_init ";" expr ";" simple_stmt ")" block
              | "for" bind [ "," bind ] "in" ( range | expr ) block ;
for_init      = type IDENT "=" expr | simple_stmt ;
range         = expr ( ".." | "..=" ) expr [ "by" expr ] ;

switch_stmt   = "switch" "(" expr ")" "{" { case_clause } [ default_clause ] "}" ;
case_clause   = "case" case_item { "," case_item } ":" statement { statement } ;
case_item     = expr [ "..=" expr ] ;               (* ".." here: C4022 *)
default_clause = "default" ":" statement { statement } ;

jump_stmt     = "break" [ IDENT ] ";" | "continue" [ IDENT ] ";"
              | "return" [ expr ] ";" | "fallthrough" ";" ;
defer_stmt    = ( "defer" | "errdefer" ) ( block | simple_stmt ";" ) ;   (* "errdefer": LS-189 *)
assert_stmt   = "assert" "(" expr [ "," STRING ] ")" ";" ;
reduce_stmt   = "reduce" IDENT "=" IDENT "(" args ")" ";" ;
                (* the in-kernel forms of SPEC-02 8.2 (LS-249): one contribution, or
                   fold_checked(add, init, e) *)
```

### 18.6 Expressions

**LS-302.** The productions of expressions:

```ebnf
expr          = cond_expr [ "catch" ( "(" IDENT ")" block | cond_expr ) ] ;
cond_expr     = or_expr [ "?" expr ":" cond_expr ] ;
or_expr       = and_expr { "||" and_expr } ;
and_expr      = cmp_expr { "&&" cmp_expr } ;
cmp_expr      = bor_expr [ cmp_op bor_expr ] ;      (* a second cmp_op is C2102 *)
cmp_op        = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
bor_expr      = bxor_expr { "|" bxor_expr } ;
bxor_expr     = band_expr { "^" band_expr } ;
band_expr     = shift_expr { "&" shift_expr } ;
shift_expr    = add_expr { ( "<<" | ">>" | "<<%" ) add_expr } ;
add_expr      = mul_expr { ( "+" | "-" | "+%" | "-%" | "+|" | "-|" ) mul_expr } ;
mul_expr      = conv_expr { ( "*" | "/" | "%" | "*%" | "*|" ) conv_expr } ;
conv_expr     = unary_expr { conv_op type [ round_clause ] } ;
conv_op       = "as" | "as%" | "as|" | "as?" ;       (* as|: Proposed *)
round_clause  = "round" IDENT ;                     (* IDENT is a mode name *)
unary_expr    = neg_literal
              | ( "-" | "-%" | "~" | "!" | "try" ) unary_expr | postfix_expr ;
neg_literal   = "-" ( DEC_INT | HEX_INT | OCT_INT | BIN_INT | TERN_INT
                    | DEC_FRAC | HEX_FRAC | CHAR ) ;
                (* chosen whenever a "-" in operand position precedes a literal token,
                   whatever whitespace or comments lie between (LS-28); binary "-" is unaffected *)
postfix_expr  = primary { "(" [ args ] ")" | "[" index_list "]" | "." IDENT } ;
args          = arg { "," arg } [ "," ] ;
arg           = expr | type | IDENT "=" expr ;
index_list    = index { "," index } ;
index         = expr | [ expr ] ( ".." | "..=" ) [ expr ] [ "by" expr ] ;

primary       = literal
              | ( DEC_FRAC | HEX_FRAC ) round_clause
              | IDENT
              | "(" expr ")"
              | "(" expr "," expr { "," expr } ")"  (* tuple in return only *)
              | "[" [ expr { "," expr } [ "," ] ] "]"
              | "." IDENT                           (* context-typed enum, error or Round *)
              | elem_type "." IDENT ;               (* I64.max, Q32.32.epsilon, Q16.16.raw *)
literal       = DEC_INT | HEX_INT | OCT_INT | BIN_INT | TERN_INT | DEC_FRAC | HEX_FRAC
              | CHAR | STRING | BYTES | "true" | "false" ;
```

Disambiguation rules:

1. **LS-303.** A statement is a declaration if it begins with a type followed by an identifier, with `const`, `var`, `in` or `inout`, or with `(` followed by a type and an identifier (typed destructuring). Otherwise it is a simple statement. Because types and values share one namespace with one definition per name, the parser may also consult declared names; both methods give the same result for every valid program.
2. **LS-304.** In a `case` item, a conditional expression must be parenthesized, so the `:` ending the case is unambiguous.
3. **LS-305.** In `for x in a..b`, the range belongs to the `for`; `in` is never an expression operator.
4. **LS-306.** `x.f(T)` with a type argument (as in `.lanes(U16)`) and `a.child(n)` are resolved as built-in members; user code cannot declare members.
5. **LS-307.** In an argument list, an argument that parses as a type name and not as a value is a type argument; only built-ins accept type arguments (section 6.8). Operation-name and mode-name positions of built-ins take a bare `IDENT` (section 6.8).

## 19. Tutorial: an exact ledger

**LS-308.** **Specified** (program and output). This program is meant to be read by a Python developer with no other introduction. Line numbers are part of the example because sections 8.9 and 17.3 refer to them.

```ci
// ledger.ci: an exact bank ledger. Run it with: cint run ledger.ci
// Money is whole cents held in I64. There is no floating point anywhere.

error LedgerError { overdrawn }

enum Kind : U8 { deposit, withdraw, interest }

struct Entry {
    Kind kind;
    I64  cents;                 // amount; for interest, the yearly rate in basis points
}

const I128 DAYS_PER_YEAR = 365; // a typed constant; I128 because it scales an I128 product

// One day of interest, rounded half to even, to the cent.
I64 daily_interest(I64 balance, I64 basis_points) {
    I128 exact = (balance as I128) * (basis_points as I128);   // cannot overflow I128
    I128 cents = div_round(exact, 10_000 * DAYS_PER_YEAR, half_even);
    return cents as I64;                                        // checked narrowing
}

// Apply one entry. Overdrawing is an expected error; overflow is a fault.
LedgerError!I64 apply(I64 balance, Entry e) {
    switch (e.kind) {
        case .deposit:
            return balance + e.cents;
        case .withdraw:
            if (e.cents > balance) {
                return LedgerError.overdrawn;
            }
            return balance - e.cents;
        case .interest:
            return balance + daily_interest(balance, e.cents);
    }
}

// Top-level statements run when the file is run, like a notebook cell.
Entry[5] book = [
    Entry(.deposit,  125_000),
    Entry(.withdraw,  40_050),
    Entry(.interest,     450),  // 4.50 % a year, for one day
    Entry(.withdraw, 200_000),  // more than the balance: rejected, not crashed
    Entry(.deposit,       99),
];

I64 balance = 0;
for i, e in book {
    balance = apply(balance, e) catch (err) {
        "{i}: rejected {err}\n";
        continue;
    };
    "{i}: {e.kind:<8} {e.cents:>8} -> {balance:/100}\n";
}
"final balance: {balance:/100}\n";

test "interest ties round to even" {
    assert(daily_interest(1_825_000, 1) == 0);  // exactly half a cent: rounds to 0
    assert(daily_interest(5_475_000, 1) == 2);  // exactly 1.5 cents: rounds to 2
}

test "overdraft is an error" {
    I64 after = apply(100, Entry(.withdraw, 101)) catch -1;
    assert(after == -1);
}

test "overflow is a fault" expect_fault E_OVERFLOW {
    _ = apply(I64.max, Entry(.deposit, 1));
}
```

**LS-309.** For `cint run ledger.ci`, a conforming implementation must print exactly:

```text
0: deposit    125000 -> 1250.00
1: withdraw    40050 -> 849.50
2: interest      450 -> 849.60
3: rejected LedgerError.overdrawn
4: deposit        99 -> 850.59
final balance: 850.59
```

**LS-310.** `cint test ledger.ci` runs the three tests and does not run the top-level statements.

What a reader should notice, in program order:

| Line | Point |
|---|---|
| 4 | `error` declares the expected failures, as values. |
| 6 | Enums have an explicit storage type. `deposit` is 0, so a zero-filled `Kind` is valid (section 4.5). |
| 13 | Every constant has a declared type. At line 18 the literal `10_000` takes `I128` from `DAYS_PER_YEAR` (section 4.2 rule 4.4), so `10_000 * DAYS_PER_YEAR` is the `I128` value 3650000, matching `exact` in `div_round`. |
| 17 to 19 | Widening is written out; the one rounding is named; narrowing back is checked. 84950 cents at 450 basis points earns 38227500 / 3650000 = 10.47 cents, which rounds to 10. |
| 23 | `LedgerError!I64` means "an `I64`, or a `LedgerError`". |
| 24 to 34 | The switch covers every `Kind`, so no `default` and no missing return. |
| 26 | `balance + e.cents` can only overflow by faulting (section 17.3 shows the report). |
| 38 to 44 | Struct values are built like calls; `.deposit` takes its enum type from the field. |
| 46 | `balance` is a script local: visible from here on, never inside `apply` (section 10). |
| 47 | `for i, e in book` is Python's `enumerate`. |
| 48 to 51 | `catch (err)` handles the expected error; the block must leave (`continue`). |
| 52 | Interpolated printing; `:<8` pads the enum name; `:/100` prints cents as exact dollars. |
| 56 to 68 | Tests sit next to the code. The last test passes only if its body faults with `E_OVERFLOW`; the fault ends that test. |

## 20. Implementation limits

**LS-311.** **Proposed.** A conforming implementation must accept programs up to these limits and must reject programs beyond its own limits with a C9xxx diagnostic, never by miscompiling or truncating.

| Quantity | Minimum supported |
|---|---|
| Source file size | 16,777,216 bytes |
| Block and expression nesting depth (excluding `else if` chains) | 256 |
| `else if` arms in one chain | limited only by file size |
| Cases in one switch | 65,536 |
| Parameters per function | 64 |
| Fields per struct; enumerators per enum | 4,096 |
| Identifier length | exactly 255 bytes (LS-15): a longer identifier is C1012 on every implementation |
| Literal magnitude | exactly 4,096 bits, a maximum (LS-31): a larger literal is C1023 on every implementation |
| Array rank | The maximum rank of the single `cint-core-1` view descriptor: 4 (SPEC-02 V-2 and SPEC-03 `CINT_MAX_RANK`, Proposed) |
| Call-depth bound accepted as a semantic input (section 6.7) | 4,096 active calls |
| Frame-arena capacity accepted as a semantic input (section 5.1) | 2,097,152 elements (LS-110) |

A program beyond an implementation's nesting limit is C9004 at the construct that exceeds it. Rows that say "exactly" or "a maximum" bind every implementation; the other rows are minimums.

For comparison, the legacy native-code profile admitted 1,048,576 bytes of source, 1,024 functions, 32 parameters, syntax nesting of 128, and 256 active calls including `main`.

## 21. Open questions

### 21.1 Questions

1. **Integer division default.** Closed: floor division, as SPEC-01 4.4 specifies (sections 7.5 and 4.2).
2. **Inexact fraction literals.** Closed: compile error C2006 unless written with a `round` clause, as SPEC-01 3.2 specifies (section 3.6).
3. **Case ranges.** Closed: case labels admit only inclusive ranges `lo..=hi`; `lo..hi` is C4022. This answers SPEC-01 Open question 11.
4. **Fault names not yet in the canonical list.** Closed: the default frame-arena capacity is 2,097,152 elements (LS-110), with OQ-68 open for measurement. A failed `assert` outside tests is closed: `E_ASSERT` (code 13, SPEC-01 IM-104). Call-depth exhaustion is closed: `E_DEPTH` with a per-entry limit (SPEC-01 9.1, 9.4; section 6.7).
5. **Named-argument spelling.** `name = value` (Python [4]) versus `name: value` (Swift [34] and C# [35]). The draft uses `=` because assignment is a statement.
6. **`var` type inference.** Whether `var` stays, given that a literal-only initializer silently defaults to `I64`.
7. **User-defined generics.** `cint-core-1` has polymorphic built-ins and the built-in parameterized types `Pool(T)` and `Handle(T)` only.
8. **Tagged unions and optional values.** Needed for parsers and containers; absent from this draft.
9. **Nested block comments** (Proposed) versus C's non-nesting comments.
10. **Unsigned fixed point** (`UQ<i>.<f>`), owned jointly with SPEC-01.
11. **Default rendering of structs** (section 9.4) and whether it must be valid source. Enum rendering is Specified.
12. **Unicode identifiers.** ASCII-only identifiers avoid confusable names [36]; they also exclude non-English codebases. Unicode identifiers, if adopted, would follow Unicode Standard Annex #31 [37].
13. **Workbench redefinition.** How live redefinition in the workbench interacts with one definition per name and with recorded checkpoints.
14. **`out` parameters in ordinary functions,** currently rejected (C5001) in favor of multiple returns.
15. **Output-coverage rule for kernels.** Closed: Specified by SPEC-02 K-10 (section 13.2 rule 5).
16. **Canonical kernel address.** Closed: dispatch numbers from 0 per entry, step ordinals by SPEC-01 9.3, `sum` narrowing reported as `E_OVERFLOW`, and `sum` over `I1024` admitted with an internal accumulator (SPEC-01 6.2, 6.3, 9.3; SPEC-02 F-1, F-6, R-5). The section 17.4 example follows them.
17. **Maximum rank and the view descriptor.** Closed: one descriptor, `cint_view` in SPEC-03 5.3, with `CINT_MAX_RANK` 4 (SPEC-02 V-2, V-17); section 20 refers to it. Whether to raise the rank is SPEC-02 O-4.
18. **`secret` qualifier** (SPEC-07 Q6). Reserved here as a keyword; its typing rules and the redacted-operand encoding belong to SPEC-07 and SPEC-01.
19. **`errdefer` reservation.** Closed: `errdefer` is Specified and reserved as a keyword (LS-18, LS-189).
20. **Citation gaps.** (a) Section 8.2 notes that `else if` chains consumed the legacy profile's nesting limit; no source for that observation has been found yet [citation needed]. (b) Closed: this document's list uses the wording of the shared bibliography, REFERENCES-P.1 ([REFERENCES-P.1.md](REFERENCES-P.1.md)). (c) The Fortran clause numbers cited in section 4.3 (8.5.8.3 and 16.9.109) were checked against the J3 working draft 18-007r1, not against the published ISO text.
21. **An explicit script marker.** An optional leading item (for example `script;`) that marks a script, to be settled with the workbench design (SPEC-06 5, T4). A mandatory marker would cut against the immediacy of top-level statements (section 10).
22. **Notes in the machine form.** Whether CINT-FAULT-1 and CINT-DIAG-1 gain a `notes` array beside `help`, for the LS-312 note (SPEC-06 8.4).
23. **Built-in names before T3.** Which built-in names stay reserved in the unqualified prelude (LS-22, LS-112), and which move behind a qualifying prelude module, reachable as `<prelude>.name` or by selective import (LS-225, LS-226). Until this is settled, every prelude name stays reserved (LS-22); qualification is to be pursued before the T3 reductions freeze, starting from this split: reserved (25), the scalar arithmetic of SPEC-01 used without imports, `abs`, `uabs`, `min`, `max`, `clamp`, `div_trunc`, `rem_trunc`, `div_euclid`, `rem_euclid`, `divmod`, `div_round`, `muldiv`, `mul_full`, `isqrt`, `isqrt_round`, the six `*_result`, `rotl`, `rotr`, `wrap_bits`, and `len`; behind qualification (28), `count`, `sum`, `sum_wrap`, `sum_sat`, `fold_checked`, `size`, `extent`, `lower`, `copy`, `fill`, `equal`, `swap`, `random`, `sign`, `format`, `utf8`, `embed`, `to_device`, `to_host`, `rescale3`, `rescale3_rem`, `shl3`, `tdot`, and the fixed-point `mul`, `div`, `mul_wrap`, `mul_sat`, and `sqrt`. Python keeps every built-in reachable through the `builtins` module [38]; Python also allows shadowing, which LS-112 does not. Evidence to gather first: the collision count in `compiler/*.ci` and `examples/`. The split lists no name of SPEC-05 section 6, `trit`, or the Kleene connectives, which are not in LS-145 yet (its SPEC-05 row).

**Open questions cited by identifier.** These questions are cited above by identifier and remain open.

- OQ-68: Whether measurement confirms 2,097,152 elements as the default and minimum supported frame-arena capacity (LS-110, section 20).
- OQ-171: How the `CINT-FAULT-1` format of SPEC-06 carries `span` (the start and end column of the carets) and the `function` of a stack entry, which the machine form of LS-289 shows.

**Findings not adopted, with rationale.**

- Reserving `from` as a keyword (for SPEC-02 V-9's `I64[n from 1]`): not adopted. Lower bounds use the range spelling of section 4.3, which keeps one meaning for `..` and `..=` everywhere and needs no new keyword.
- Keeping chained comparisons by amending SPEC-01 4.8: not adopted. SPEC-01's rejection stands; it needs no rule for single evaluation of the middle operand or fault order inside a chain.
- Keeping integer to `Bool` `as`: not adopted; SPEC-01 IM-10 stands (C2056).
- `switch` directly on an error union (SPEC-01 4.1, 9.4): not adopted; bind with `catch (e)` and switch on the error value (section 4.6), which needs no tagged-union syntax.
- Typed tuple declaration through an untyped `var` only: not adopted; the typed form `(I64 q, I64 r) = f();` is added because reviewers can read the types (section 4.7).
- Comprehension reductions (`sum(j in 0..n where j != i) { ... }`, SPEC-08): not adopted; block expressions are absent by design (section 6.5). Write an explicit loop with an `I128` accumulator.

### 21.2 Corrections filed against companion specifications

Each row is a change the named section had to make so that its examples parse and mean the same under this document. Every row has been applied; the table is kept as a record.

| Section | Current text | Correction |
|---|---|---|
| SPEC-01 2.2 | "must not narrow `I64` to 32 bits" | Add that a range-proven representation is not narrowing (section 13.2). |
| SPEC-01 3.2, CIF-1 fixtures 54, 56 | constant out of range is "compile error with code `E_NARROW`" | `C2003` (section 17.2). |
| SPEC-01 4.1, 9.4 | result-returning unions "consumed with `try` or a `switch`" | `try`, `catch`, or `_ =`; switch on the bound error (section 4.6). |
| SPEC-01 6.2 | `I8[3] v = {120, 120, -120};` | `I8[3] v = [120, 120, -120];` |
| SPEC-01 9.1, 10 | no call-depth fault or bound | Add the call-depth semantic input and its fault (section 6.7). |
| SPEC-01 Open question 5 | spellings "need confirmation" | Confirmed: `sum(R, xs)`, `x as I32 round floor` (section 6.8). |
| SPEC-02 K-14, 15.2, 15.6 | `try diffuse(a, 1, 8, b, total);`, `try cool(t, 1);` | `diffuse(a, 1, 8, b, total);`, `cool(t, 1);` (C2043). |
| SPEC-02 V-9 | `I64[n from 1]` | `I64[1..=n]` (section 4.3). |
| SPEC-02 V-10 | `I32[8, 8] left = m[.., 0..8];` described as a view; `I32[8, 16] m = 0;` | `inout I32[8, 8] left = m[.., 0..8];`; `I32[8, 16] m;` (zero-filled). |
| SPEC-02 15.2 | `I32[8, 8] a = 0;` | `I32[8, 8] a;` |
| SPEC-03 3.2 examples | `Handle<Body>`, `Pool<Body>`, `arena_create<I64>`, `record`, `Body{ mass = 7, ... }` | `Handle(Body)`, `Pool(Body)`, `struct`, `Body(mass = 7, ...)`. |
| SPEC-03 M-23 | compile error "with code `E_ALIAS`" | `C5011` (or C5010 for argument overlap). |
| SPEC-03 M-29 | `in I64[1..m, 1..n] a` admits `a[m, n]` | `in I64[1..=m, 1..=n] a`. |
| SPEC-03 5.3 | `cint_fault` with 4 operands of 128 bytes; `cint_elem` lists only `Q16.16`, `Q32.32` | Variable-length operands (no truncation of canonical fields, section 17.3); fixed point encoded by storage width and `frac_bits`. |
| SPEC-05 TR-DOT-1, SPEC-07 | `tdot<I32>(w, x)` | `tdot(I32, w, x)` (section 6.8). |
| SPEC-06 3.5, 4.3 | fuel "steps"; default 1,000,000,000 steps | `fuel-v1` units (section 14). |
| SPEC-06 8.5 | `C_` names | Numeric `C` codes (section 17.2); the suggestion policy is shared (section 17.6). |
| SPEC-08 10.4 | `mul_wide`, `isqrt_floor`, `div_rne`; `(I64 whole, I64 rem) = split_floor(...)`; `sum(j in 0..n where j != i) { ... }` | `mul_full`, `isqrt`, `div_round(a, b, half_even)`; the typed destructuring is valid; the comprehension becomes an explicit loop in SPMD form with an `I128` accumulator. |
| SPEC-09 5.4 | `case 'a'..'z': case 'A'..'Z': case '_': return TK_IDENT;`; `record`; `out Token[cap]` in a function; undeclared size parameters | `case 'a'..='z', 'A'..='Z', '_': return TK_IDENT;`; `struct`; an `inout` parameter or a tuple result; declared size parameters. Add a conformance case for a range switch on character data. |

## 22. Out of scope

This document deliberately does not specify:

- The meaning of arithmetic, rounding modes, shift and division edge cases, wide-integer limb layout, fixed-point intermediate widths, and reductions (SPEC-01).
- Kernel execution, publication on success, in-place operations, canonical fault ordering, backend capability rules, and schedule directives (SPEC-02).
- Trit encodings, packed layouts, `rescale3`, and `tdot` (SPEC-05).
- Buffers, arenas, generation-checked handles, view descriptors, and placement (SPEC-02, SPEC-03).
- The C ABI, Python entry points, Fortran `ISO_Fortran_binding.h` wrappers (ISO/IEC 1539-1:2018, section 18.5 [20]), the HLA [39] [40] [41] and SpaceFOM [42] gateway, and classification of foreign calls (SPEC-03). This document cites the 2010 (HLA Evolved) editions of IEEE 1516, 1516.1, and 1516.2; IEEE has since superseded them with the 2025 editions. Which edition the gateway targets is for SPEC-03 to state.
- Workbench commands, checkpoints, replay, the differential debugger, and the `CINT-FAULT-1` JSON schema (SPEC-06).
- The project manifest, `cint build`, signing, and build records (SPEC-07).
- The standard library beyond the reserved built-in names of section 6.8.
- Concurrency other than kernels, input other than declared host inputs, and any floating-point boundary conversion.
- Homoglyph and confusable characters inside string literals and comments (CVE-2021-42694 class). Identifiers are ASCII (section 3.1), which excludes them from names; review tools may flag them in strings.
- Usability claims. Goal 1 of section 2 is an aim, and no user study has been run yet.
- The legacy profile `cint-bt27-legacy`, which remains frozen.

## References

Entries use the wording of the shared bibliography, REFERENCES-P.1 ([REFERENCES-P.1.md](REFERENCES-P.1.md)); the bracketed key names the shared entry. A "Used here" note names the parts this document relies on.

1. [ISO-9899-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2018, Information technology: Programming languages: C* (C17). International Organization for Standardization, 2018. https://www.iso.org/standard/74528.html. ISO has replaced it with ISO/IEC 9899:2024. Working drafts with the same clause numbering: WG 14 N2176 (ballot text) and N2310, https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2310.pdf. Used here: sections 6.3.1.1 (integer promotions), 6.4.4.1 (integer constants), and 6.5 (expressions, operator precedence, and floating-point contraction).
2. [PEP-515] Georg Brandl and Serhiy Storchaka. *PEP 515: Underscores in Numeric Literals*. Python Enhancement Proposal, 2016. https://peps.python.org/pep-0515/.
3. [PSF-Functions-Format] Python Software Foundation. "Built-in Functions" and "string: Common string operations" (Format Specification Mini-Language). *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/functions.html and https://docs.python.org/3/library/string.html#formatspec. Used here: `range`, `enumerate`, and the Format Specification Mini-Language.
4. [PSF-LangRef] Python Software Foundation. *The Python Language Reference*, Python 3.14. https://docs.python.org/3/reference/. Used here: sections "Lexical analysis" (f-strings), 6.7 "Binary arithmetic operations" (floor division and the sign of `%`), 6.10 "Comparisons" (chained comparisons), and "Calls" (keyword arguments).
5. [Davis-HolyC] Terry A. Davis. "HolyC." TempleOS documentation, file `Doc/HolyC.DD` (undated). No publisher-maintained locator exists; community archive of the TempleOS source: https://github.com/cia-foundation/TempleOS/blob/archive/Doc/HolyC.DD. Read 2026-10-02. Used here: the archived text states that functions with no arguments can be called without parentheses and that code outside functions runs at start-up, in order.
6. [Jupyter-Notebook] Project Jupyter. "The Jupyter Notebook." *Jupyter Notebook documentation*. https://jupyter-notebook.readthedocs.io/en/stable/notebook.html. Read 2026-10-02. Used here: section "Basic workflow," "Keyboard shortcuts" (Shift-Enter executes the current cell and shows any output).
7. [GCC-Case-Ranges] Free Software Foundation. "Case Ranges." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Case-Ranges.html. Read 2026-10-02.
8. [PSF-WhatsNew-3.8] Python Software Foundation. "What's New In Python 3.8." 2019. https://docs.python.org/3/whatsnew/3.8.html. Used here: section "f-strings support = for self-documenting expressions and debugging."
9. [pytest-assert] pytest developers. "How to write and report assertions in tests." *pytest documentation*. https://docs.pytest.org/en/stable/how-to/assert.html. Read 2026-10-02. Used here: assertion rewriting.
10. [RFC-3629] F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003. https://www.rfc-editor.org/rfc/rfc3629.
11. [UAX-9] The Unicode Consortium. *Unicode Standard Annex #9: Unicode Bidirectional Algorithm*, revision 52, 2026-09-01. https://www.unicode.org/reports/tr9/. Used here: explicit directional embedding, override, and isolate characters.
12. [UAX-44] The Unicode Consortium. *Unicode Standard Annex #44: Unicode Character Database*, revision 38, 2026-09-02. https://www.unicode.org/reports/tr44/. Used here: property `Default_Ignorable_Code_Point`, file `DerivedCoreProperties.txt`.
13. [Boucher-2021] Nicholas Boucher and Ross Anderson. "Trojan Source: Invisible Vulnerabilities." arXiv:2111.00169, 2021 (revised 2023). https://arxiv.org/abs/2111.00169.
14. [MITRE-CVE-2021-42574] MITRE. *CVE-2021-42574* (bidirectional control characters in source code, "Trojan Source"). CVE record published 2021-11-01. https://www.cve.org/CVERecord?id=CVE-2021-42574. Used here: the assignment is confirmed by the researchers' page, https://trojansource.codes/.
15. [MITRE-CVE-2021-42694] MITRE. *CVE-2021-42694* (homoglyphs in source code, "Trojan Source"). CVE record published 2021-11-01. https://www.cve.org/CVERecord?id=CVE-2021-42694. Used here: the assignment is confirmed by the researchers' page, https://trojansource.codes/.
16. [Unicode-17.0] The Unicode Consortium. *The Unicode Standard, Version 17.0.0*. The Unicode Consortium, 2025. https://www.unicode.org/versions/Unicode17.0.0/. Used here: chapter 3, section 3.9, definition D76 (Unicode scalar value), https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/.
17. [MS-CSharp-Integer-Literals] Microsoft. "Lexical structure," section 6.4.5.3, "Integer literals." *C# language specification*, *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/lexical-structure#6453-integer-literals. Read 2026-10-03. Used here: the negative-literal case of section 6.4.5.3 (LS-28); the section number differs between editions, and no fixed ECMA-334 edition has been checked.
18. [MS-CSharp-Lexical-General] Microsoft. "Lexical structure," section 6.3.1, "General." *C# language specification*, *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/lexical-structure#631-general. Read 2026-10-03 (source: `dotnet/csharpstandard` at `cc01bf4`, `standard/lexical-structure.md`).
19. [Knuth-1997] Donald E. Knuth. *The Art of Computer Programming, Volume 2: Seminumerical Algorithms*, 3rd edition. Addison-Wesley, 1997. ISBN 978-0-201-89684-8. Used here: section 4.1, "Positional Number Systems" (balanced ternary).
20. [ISO-1539-1-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 1539-1:2018, Information technology: Programming languages: Fortran: Part 1: Base language* (Fortran 2018). International Organization for Standardization, 2018. https://www.iso.org/standard/72320.html. ISO has replaced it with ISO/IEC 1539-1:2023 (Fortran 2023). Final committee draft: J3/18-007r1, https://j3-fortran.org/doc/year/18/18-007r1.pdf. Used here: sections 8.5.8.3 (assumed-shape array), 16.9.109 (`LBOUND`), and 18.5 (the source file `ISO_Fortran_binding.h`), clause numbers checked against J3/18-007r1.
21. [SysV-AMD64-ABI] x86-64 psABI project. *System V Application Binary Interface: AMD64 Architecture Processor Supplement*, current version. https://gitlab.com/x86-psABIs/x86-64-ABI. Read 2026-10-02. Used here: section "Data Representation" (aggregates, unions, and bit-fields).
22. [MS-x64-Conventions] Microsoft. "x64 ABI conventions." *Microsoft Learn*. https://learn.microsoft.com/en-us/cpp/build/x64-software-conventions. Read 2026-10-02. Used here: section "x64 type and storage layout."
23. [Arm-AAPCS64] Arm Limited. *Procedure Call Standard for the Arm 64-bit Architecture (AArch64)* (AAPCS64), release 2025Q4, 23 January 2026. https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst.
24. [Zig-LangRef] Zig Software Foundation. *Zig Language Reference* (master). https://ziglang.org/documentation/master/. Read 2026-10-02. Used here: sections "Errors" (error sets, error union types, `try`, `catch`, `errdefer`) and "defer."
25. [Pharr-2012] Matt Pharr and William R. Mark. "ispc: A SPMD Compiler for High-Performance CPU Programming." *2012 Innovative Parallel Computing (InPar)*, pages 1-13, 2012. DOI 10.1109/InPar.2012.6339601.
26. [RaganKelley-2013] Jonathan Ragan-Kelley, Connelly Barnes, Andrew Adams, Sylvain Paris, Frédo Durand, and Saman Amarasinghe. "Halide: A Language and Compiler for Optimizing Parallelism, Locality, and Recomputation in Image Processing Pipelines." *Proceedings of the 34th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2013)*, pages 519-530, 2013. DOI 10.1145/2491956.2462176.
27. [Khronos-Vulkan] The Khronos Vulkan Working Group. *Vulkan API Specification* (Vulkan 1.4.365 with all registered extensions on 2026-10-02). The Khronos Group. https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html; chapters and reference pages at https://docs.vulkan.org/. Read 2026-10-02. Used here: chapter "Features," feature `shaderInt64`, https://docs.vulkan.org/spec/latest/chapters/features.html.
28. [MS-CSharp-Constant-Expressions] Microsoft. "Expressions," section 12.26, "Constant expressions." *C# language specification*, *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/expressions#1226-constant-expressions. Read 2026-10-03. The ECMA-334 working drafts number the section 12.25.
29. [SEI-CERT-INT02-C] Software Engineering Institute, Carnegie Mellon University. "INT02-C. Understand integer conversion rules." *SEI CERT C Coding Standard* (online edition). https://wiki.sei.cmu.edu/confluence/spaces/c/pages/87152206/INT02-C.+Understand+integer+conversion+rules. Unverified: on 2026-10-02 this locator redirected to a page-not-found on the standard's new site, https://cmu-sei.github.io/secure-coding-standards/, whose index still lists INT02-C.
30. [MITRE-CWE-484] MITRE. "CWE-484: Omitted Break Statement in Switch." *Common Weakness Enumeration*, version 4.20. https://cwe.mitre.org/data/definitions/484.html.
31. [MISRA-C-2025] MISRA Consortium. *MISRA C:2025, Guidelines for the use of the C language in critical systems*. The MISRA Consortium, 2025. https://misra.org.uk/misra-c/.
32. [SEI-CERT-C-2016] Software Engineering Institute, Carnegie Mellon University. *SEI CERT C Coding Standard: Rules for Developing Safe, Reliable, and Secure Systems*, 2016 edition. 2016. https://www.sei.cmu.edu/library/sei-cert-c-coding-standard-rules-for-developing-safe-reliable-and-secure-systems-2016-edition/.
33. [RFC-8259] T. Bray, editor. *The JavaScript Object Notation (JSON) Data Interchange Format*. RFC 8259 (STD 90), December 2017. https://www.rfc-editor.org/rfc/rfc8259.
34. [Swift-Book] Apple Inc. and the Swift project. "Functions" (section "Function Argument Labels and Parameter Names"). *The Swift Programming Language*, source file `TSPL.docc/LanguageGuide/Functions.md`. https://github.com/swiftlang/swift-book/blob/main/TSPL.docc/LanguageGuide/Functions.md. Read 2026-10-02.
35. [MS-CSharp-Named-Args] Microsoft. "Named and Optional Arguments (C# Programming Guide)." *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/named-and-optional-arguments. Read 2026-10-02.
36. [UTS-39] The Unicode Consortium. *Unicode Technical Standard #39: Unicode Security Mechanisms*, revision 34, 2026-08-27. https://www.unicode.org/reports/tr39/. Used here: section 4 (confusable detection).
37. [UAX-31] The Unicode Consortium. *Unicode Standard Annex #31: Unicode Identifiers and Syntax*, revision 45, 2026-09-01. https://www.unicode.org/reports/tr31/.
38. [PSF-builtins] Python Software Foundation. "builtins: Built-in objects." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/builtins.html. Read 2026-10-03.
39. [IEEE-1516-2010] IEEE. *IEEE Std 1516-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Framework and Rules*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5553440. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516-2025 (HLA 4), https://standards.ieee.org/ieee/1516/6687/.
40. [IEEE-1516.1-2010] IEEE. *IEEE Std 1516.1-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Federate Interface Specification*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5557728. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516.1-2025.
41. [IEEE-1516.2-2010] IEEE. *IEEE Std 1516.2-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Object Model Template (OMT) Specification*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5557731. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516.2-2025.
42. [SISO-STD-018-2020] Simulation Interoperability Standards Organization (SISO). *SISO-STD-018-2020, Standard for Space Reference Federation Object Model (SpaceFOM)*, version 1.0 (document dated 25 October 2019; approved by the SISO Standards Activity Committee on 18 December 2019 and by the SISO Executive Committee on 9 January 2020). SISO, 2020. https://cdn.ymaws.com/www.sisostandards.org/resource/resmgr/standards_products/siso-std-018-2020_srfom.pdf.
