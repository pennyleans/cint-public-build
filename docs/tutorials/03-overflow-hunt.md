<img src="../../assets/brand/cint-logo.svg" alt="CINT" width="96">

# Tutorial 03: hunting an overflow

Status: tutorial, milestone M2 of slice 2, 2026-10-05. Written for a developer who has read tutorial 01 and knows C, C#, or Python. Every output below is pasted from a run on Linux (Ubuntu 24.04) with the GCC and Clang legs; where a run has a receipt, the receipt is named. The MSVC leg on Windows has not run these programs yet.

In this tutorial a program adds up file sizes in a 32-bit integer and stops with a fault. You read the fault record to find the exact addition that failed and the call that led to it, then fix the program three ways and see what each fix means. The last section is about a minus sign and a pair of parentheses.

## 1. The program

`examples/overflow_hunt.ci`:

```ci
// overflow_hunt.ci: an I32 total that does not fit.
// Run it with: cint run examples/overflow_hunt.ci
// Tutorial 03 reads the fault record and fixes the program three ways.

// The total size, in bytes, of a list of files.
I32 total_bytes[n](in I32[n] sizes) {
    I32 total = 0;
    for i in 0..n {
        total = total + sizes[i];
    }
    return total;
}

void main() {
    I32[4] sizes;
    sizes[0] = 700_000_000;    // four files of a backup, in bytes
    sizes[1] = 900_000_000;
    sizes[2] = 650_000_000;
    sizes[3] = 120_000_000;
    "files: 4\n";
    I32 total = total_bytes(sizes);
    "total: {total} bytes\n";
}
```

`I32` is a signed 32-bit integer: it holds exactly the values from -2147483648 to 2147483647. `total_bytes[n](in I32[n] sizes)` takes a read-only view of an array of any length `n` (SPEC-04 4.3, 6.2); `main` passes its four-element array, so `n` is 4. The statements are in `void main() { ... }` rather than at the top level, because a top-level statement, even a print, makes the file a script, and a script's variables are not visible inside its functions or tests (SPEC-04 LS-218, LS-219; C3004).

The four sizes add up to 2,370,000,000 bytes, which is more than `I32` can hold.

## 2. Run it

```text
$ cint run --receipt results/cint/t2/demos/overflow_hunt/run-overflow_hunt-gcc.json examples/overflow_hunt.ci
files: 4
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> overflow_hunt.ci:9:23
   |
 9 |         total = total + sizes[i];
   |                       ^ add.checked.i32
   |
   = operation: add.checked.i32
   = operand-count: 2
   = operand: I32 1600000000
   = operand: I32 650000000
   = exact:   2250000000
   = limit:   I32 2147483647
   = revision: 82ac5433d3d274b53a564fba6eb26d78d88be24d55186efd67f232d356c601fa
   = source-map: none
   = address: none
   = stack-depth: 1
   = called from: overflow_hunt.ci:21:17
   = fuel-consumed: 5 (not part of the fault record)
   = state: the program stopped; stdout holds what it wrote before the fault
receipt: results/cint/t2/demos/overflow_hunt/run-overflow_hunt-gcc.json
```

The exit status was 1, a run-time fault (SPEC-06 3.2). Only the first line, `files: 4`, is stdout: 9 bytes, the same bytes as `examples/overflow_hunt.stdout`. Everything from `fault[E_OVERFLOW]` on is stderr (SPEC-06 3.4a). The second print never ran, so no wrong total was ever printed.

Measured: `results/cint/t2/demos/overflow_hunt/run-overflow_hunt-gcc.json` and `run-overflow_hunt-clang.json`, 2026-10-05, source revision `d2914c5`. Both record outcome `fault`, exit status 1, the 9 stdout bytes (SHA-256 `d7bd6d1d...d0bd19`), every field of the record above, and `fuel_consumed` 5; `python tools/cint_receipts.py identity results/cint/t2/demos/overflow_hunt` ends with `1 receipt identity in 2 receipts`. Reported: `cint_ref run examples/overflow_hunt.ci` at the same commit gives the same code, operation, operands, exact value, limit, position, stack depth, stdout bytes, and `fuel-consumed 5`.

## 3. Reading the record

Tutorial 01 explains each field once. Here is what each one says about this program.

- `fault[E_OVERFLOW]`: a checked operation had an exact result outside its type's range.
- `operation: add.checked.i32`: the plain `+` (the checked form) on `I32` (SPEC-04 LS-284).
- `operand: I32 1600000000` and `operand: I32 650000000`: the two values that were added, with their types, in source order. The first is `total` after two files (700,000,000 + 900,000,000); the second is `sizes[2]`. So the fault is in the third trip through the loop, when `i` was 2.
- `exact: 2250000000`: the true mathematical sum, 1,600,000,000 + 650,000,000. It is printed without a type, because it is the value that has no `I32` representation.
- `limit: I32 2147483647`: the bound it crossed, `I32.max`.
- `--> overflow_hunt.ci:9:23`: line 9, column 23, the `+` token. A position always points at the operator that faulted, not at the start of the statement (SPEC-04 LS-278).
- `stack-depth: 1` and `called from: overflow_hunt.ci:21:17`: the record's stack holds the call-site positions of the user-function calls that were active, outermost first (SPEC-01 IM-106). There is one: line 21, column 17, where the callee's name `total_bytes` starts in the call `total_bytes(sizes)` in `main` (a call site is the callee name, SPEC-01 IM-107). `main` itself is the entry that `cint run` called, a host call with no call site (SPEC-01 IM-107). In tutorial 01 the stack depth was 0, because the fault was in the script's own body.
- `revision`: the revision identity of the program (SPEC-01 IM-157). It is computed over the program's meaning, not its text, so it does not by itself pin `9:23` to a text: a blank line above line 9 moves the position and keeps the revision. The source map does that job (SPEC-06 section 1.3), and `cint run` does not produce one yet, so `source-map` is `none`. The receipt records the SHA-256 of the source file, which ties the position to the bytes that ran.
- `fuel-consumed: 5`: one unit for entering `main`, one for the call of `total_bytes`, and one for each of the three loop iterations that started (SPEC-01 10.2). It is printed beside the record, not in it, because fuel belongs to the run's result rather than to the fault.

With `cint run --json`, stderr holds one `CINT-FAULT-1` JSON object instead of the text above, and the call site appears as `"stack":[{"column":17,"file":"overflow_hunt.ci","line":21}]` (SPEC-06 3.4a).

The record already answers the hunt's three questions: which operation (the `+` on line 9), with which values (1,600,000,000 and 650,000,000), and reached from where (the call on line 21).

### What other languages do here

| Language | `total + sizes[i]` past the type's maximum |
| --- | --- |
| C17, `int32_t` | The behavior is undefined: C17 6.5 paragraph 5 says that if a result is not in the range of representable values for its type, the behavior is undefined [1]. An unsigned type wraps modulo 2^32 instead (6.2.5 paragraph 9) [1]. |
| C#, `int` | The default context for non-constant expressions is `unchecked`, where addition wraps from the maximum value to the minimum value; inside `checked` it throws `System.OverflowException` [2]. |
| Python, `int` | No overflow: an `int` has no fixed size [3]. |
| NumPy, `np.int32` | Fixed-size integer types overflow, as NumPy's guide warns [4]. |
| CINT, `I32` | `+` is always checked: the program stops with `E_OVERFLOW` and the record above. Wrapping and saturating are separate operators, written where they are meant (SPEC-04 LS-156, LS-157). |

### Ranges, literals, and no promotion

Four habits from other languages do not carry over. Each integer type holds exactly the range below (SPEC-04 4.1, LS-50; `T.min` and `T.max` name the bounds, LS-51):

| CINT | Range | C17 | C# | NumPy |
| --- | --- | --- | --- | --- |
| `I8` | -128 to 127 | `int8_t` | `sbyte` | `int8` |
| `I16` | -32768 to 32767 | `int16_t` | `short` | `int16` |
| `I32` | -2147483648 to 2147483647 | `int32_t` | `int` | `int32` |
| `I64` | -9223372036854775808 to 9223372036854775807 | `int64_t` | `long` | `int64` |
| `U8` | 0 to 255 | `uint8_t` | `byte` | `uint8` |
| `U16` | 0 to 65535 | `uint16_t` | `ushort` | `uint16` |
| `U32` | 0 to 4294967295 | `uint32_t` | `uint` | `uint32` |
| `U64` | 0 to 18446744073709551615 | `uint64_t` | `ulong` | `uint64` |

1. **No promotion.** An operation runs in the type of its operands, and both operands must have the same type (SPEC-04 LS-156). Nothing converts implicitly, not even to a wider type (LS-134). With `U8 a = 250; U8 b = 10;`, the line `U8 c = a + b;` faults `E_OVERFLOW` with `add.checked.u8`, exact 260, limit `U8 255`. C promotes both operands to `int` first, so the sum is 260, and storing it in a `uint8_t` keeps 4 (C17 6.3.1.1 paragraph 2, 6.3.1.3 paragraph 2) [1]. C# also adds two `byte` values as `int`, and needs a cast to store the result in a `byte` [5]. NumPy adds two `uint8` arrays in `uint8`, and the sum wraps to 4 [4]. With `U16 b = 10;`, the line `U16 c = a + b;` is compile error C2001 at the `b`; write `U16 c = (a as U16) + b;`, which prints 260.
2. **Unsigned arithmetic is checked too.** With `U32 capacity = 10; U32 used = 12;`, the line `U32 left = capacity - used;` faults `E_OVERFLOW` with `sub.checked.u32`, exact -2, limit `U32 0`. In C and in unchecked C#, unsigned arithmetic wraps, so `left` would be 4294967294 [1] [2].
3. **Negation is checked.** A signed type has one more negative value than positive ones, so the negation of `T.min` does not fit. With `I32 m = I32.min;`, the line `I32 p = -m;` faults `E_OVERFLOW` with `neg.checked.i32`, operand `I32 -2147483648`, exact 2147483648, limit `I32 2147483647`.
4. **A literal is typed before the arithmetic.** A literal has no type of its own: it takes the type of its context, here the declared type, and must fit it (SPEC-04 LS-53, LS-56). An expression of literals is then evaluated as it would be at run time, in that type, never folded in exact arithmetic first (LS-54). So `I8 pct = 200 - 150;` is compile error C2003 at the `200`, because 200 is not an `I8`, although the difference, 50, is. C, C#, and Python all fold the constant to 50. To compute in a type that holds every operand, write `I16 diff = 200 - 150;` and then `I8 pct = diff as I8;`, a checked conversion that faults `E_NARROW` if the value does not fit (SPEC-04 7.5, LS-133); here it prints `pct=50`.

Reported: each example ran with `cint_ref` and with `cint run` (GCC leg) as the body of `void main()`, 2026-10-05, with the outcome, code, operation, operands, exact value, limit, and position the text gives; scratch programs, not committed. The C values 260, 4, and 4294967294 come from a scratch program built with GCC 13.3 and `-std=c17`, and the NumPy values 4 and 4294967294 from NumPy 2.4.6 arrays, the same day. The C# behavior is cited, not run.

## 4. Three fixes

A diagnostic offers at most one suggestion, and it never replaces a checked operator with a wrapping or saturating one (SPEC-04 LS-292). For an overflow, the suggestion is to widen first and keep the exact value. The workbench session of SPEC-06 section 16.1 also prints what `+%` and `+|` would do, each marked "explanation only". This section runs all three. Each is a different program with a different meaning; only the first is a fix that keeps the check. All three are in `examples/`, and each changes only what its header comments name.

### Fix 1: widen to `I64` (keeps the exact value and the check)

`examples/overflow_hunt_widened.ci` changes four lines, 6, 7, 9, and 21:

```ci
I64 total_bytes[n](in I32[n] sizes) {
    I64 total = 0;
    ...
        total = total + (sizes[i] as I64);
    ...
    I64 total = total_bytes(sizes);
```

```text
$ cint run examples/overflow_hunt_widened.ci
files: 4
total: 2370000000 bytes
```

`sizes[i] as I64` converts each size to `I64` before it is added. Widening never fails, because every `I32` value is an `I64` value. The addition is still the checked `+`, now `add.checked.i64`: it would still fault, at 9223372036854775807 instead of at 2147483647. This is the fix that keeps the check. The sizes stay `I32`; only the total, which is what grew, is wider.

### Fix 2: wrap with `+%` (a different program, not a fix)

`examples/overflow_hunt_wrapped.ci` changes line 9 to `total = total +% sizes[i];`:

```text
$ cint run examples/overflow_hunt_wrapped.ci
files: 4
total: -1924967296 bytes
```

`+%` is addition modulo 2^32: it keeps the low 32 bits of the exact sum and never faults (SPEC-04 LS-157). The third addition gives 2,250,000,000 - 4,294,967,296 = -2,044,967,296, and adding 120,000,000 gives -1,924,967,296. The program completes and prints a negative file size. That is what C# prints without `checked`, and one possible result of the undefined behavior in C. Wrapping is the right operator only when the arithmetic is meant to be modulo 2^32, as in a hash, a checksum, or a counter that is expected to roll over. It is not a fix for a total.

### Fix 3: saturate with `+|` (a different program, not a fix)

`examples/overflow_hunt_saturated.ci` changes line 9 to `total = total +| sizes[i];`:

```text
$ cint run examples/overflow_hunt_saturated.ci
files: 4
total: 2147483647 bytes
```

`+|` clamps to the type's range: a sum above `I32.max` becomes `I32.max` (SPEC-04 LS-157). The third addition gives 2147483647, and so does the fourth. The printed number is a lower bound, "at least 2147483647", not the total. Saturating is right when a cap is the intent, as for a progress bar or a value that must stay inside a range. It hides how far past the limit the true value was.

Measured: the receipts of all three fixes are under `results/cint/t2/demos/overflow_hunt_widened/`, `overflow_hunt_wrapped/`, and `overflow_hunt_saturated/`, 2026-10-05, source revision `d2914c5`, GCC and Clang, each with outcome `value`, exit status 0, stdout bytes identical to the `.stdout` file of the same name (33, 34, and 33 bytes), and one receipt identity per program.

| Fix | Line 9 | Prints | Keeps the check | Right when |
| --- | --- | --- | --- | --- |
| Widen | `total = total + (sizes[i] as I64);` | `2370000000` | yes | the exact total is needed |
| Wrap | `total = total +% sizes[i];` | `-1924967296` | no | the arithmetic is modulo 2^32 |
| Saturate | `total = total +\| sizes[i];` | `2147483647` | no | a cap is the intent |

## 5. A minus sign and parentheses

Overflow can also be found before a program runs. A literal that does not fit its type is compile error C2003, and an operation on constants that faults is compile error C6001, which names the fault (SPEC-04 LS-54). Where the minus sign sits decides which one you get. In the wording of the ruling (SPEC-04 LS-28; decision 2026-10-03, slice 2 patch D-21):

> `-(128)` is never a negative literal, however it is spaced. The parentheses make `128` an operand of its own, typed in the context before the minus applies. In context `I8`, `I8 a = -128;` and `I8 a = - 128;` are -128, but `I8 a = -(128);` is C2003 at the `128`, because 128 is not an `I8`. In a wider type the value can match while the typing differs: `I64 e = -(128);` is the negation of an `I64` 128. `U8 u = -(1);` is C6001 `E_OVERFLOW` `neg.checked.u8` with exact -1, while `U8 u = -1;` and `U8 u = - 1;` are C2003 at the `-`. As a conversion source, `-(128) as I8` is C2101 (LS-151); write `-128 as I8`.

Each line below was checked with `cint_ref` as the body of `void main() { <line> "<name>={<name>}\n"; }`, so the declaration is on line 2 and its first column is 5.

| Line 2 | Outcome under `cint_ref` |
| --- | --- |
| `I8 a = -128;` | runs, prints `a=-128` |
| `I8 a = - 128;` | runs, prints `a=-128` |
| `I8 a = -(128);` | C2003 at 2:14, the `128` |
| `I64 e = -(128);` | runs, prints `e=-128` |
| `U8 u = -(1);` | C6001 at 2:12, the `-`: `E_OVERFLOW`, `neg.checked.u8`, operand `U8 1`, exact -1, limit `U8 0` |
| `U8 u = -1;` | C2003 at 2:12, the `-` |
| `U8 u = - 1;` | C2003 at 2:12, the `-` |
| `I8 a = -(128) as I8;` | C2101 at 2:12, the `-` |
| `I8 a = -128 as I8;` | runs, prints `a=-128` |

Reported: `cint_ref` at commit `d2914c5`, 2026-10-05, nine scratch programs (not committed); the `cint run` of the same commit gave the same outcome, code, and position for each (GCC leg). The rule behind the table: a minus directly before a literal, with or without spaces, is part of the literal; a minus before anything else, a parenthesized literal included, is the negation operator, checked like any other operation.

## 6. What this tutorial does not show

- The MSVC leg. The receipts above are from GCC and Clang on Linux.
- A debugger. SPEC-06 section 16.3 shows how the workbench will find an overflow by replay and trace; the workbench is not built yet.
- Nothing here is a performance claim. The receipts record build and run times as observations only.

## References

1. [ISO-9899-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2018, Information technology: Programming languages: C* (C17). International Organization for Standardization, 2018. https://www.iso.org/standard/74528.html. Sections 6.5 paragraph 5, 6.2.5 paragraph 9, 6.3.1.1 paragraph 2, and 6.3.1.3 paragraph 2, cited from the standard's text as known on 2026-10-05; not re-read for this tutorial.
2. [MS-CSharp-Checked] Microsoft. "The checked and unchecked statements (C# reference)." *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/statements/checked-and-unchecked. Read 2026-10-05.
3. [PSF-DataModel] Python Software Foundation. "The standard type hierarchy: numbers.Integral." *The Python Language Reference*, Python 3.14, section 3.2. https://docs.python.org/3/reference/datamodel.html. Read 2026-10-03.
4. [NumPy-Types] NumPy Developers. "Data types: Overflow errors." *NumPy User Guide*, NumPy 2.5. https://numpy.org/doc/stable/user/basics.types.html#overflow-errors. Read 2026-10-03.
5. [MS-CSharp-Arithmetic] Microsoft. "Arithmetic operators (C# reference)." *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/operators/arithmetic-operators. The rule that `sbyte`, `byte`, `short`, and `ushort` operands are converted to `int` is cited from the page as known on 2026-10-05; not re-read for this tutorial.
