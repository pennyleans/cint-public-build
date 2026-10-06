<img src="../../assets/brand/cint-logo.svg" alt="CINT" width="96">

# Tutorial 02: an exact ledger

Status: tutorial, milestone M2 of slice 2, 2026-10-05. Written for a Python developer who has read tutorial 01. Every output below is pasted from a run on Linux (Ubuntu 24.04) with the GCC and Clang legs; where a run has a receipt, the receipt is named. The MSVC leg on Windows has not run this program yet. Updated the same day for roadmap box 07: `daily_interest` calls `muldiv`, and section 4 runs the tests with `cint test`. Updated again when `cint run` gained format specifications: `print_dollars` prints with `{cents:/100}` and `print_entry` with `{e.cents:>8}`.

In this tutorial you run a small bank ledger that keeps money as whole cents in a 64-bit integer, rounds interest by a stated rule, rejects an overdraft as an ordinary outcome, and treats an overflow as a fault. Then you read the three tests that sit next to the code.

## 1. Why cents, and not floats

Python's own tutorial shows the problem with binary floating point: 0.1 is not exactly 1/10, so `0.1 + 0.1 + 0.1 == 0.3` is `False` [1]. For exact decimal amounts it points to the `decimal` module, "suitable for accounting applications" [1]. CINT has no floating point at all (SPEC-04 section 2). Money is a count of cents in an `I64`, so every amount is exact, and the only question left is what happens when a result does not fit, which tutorial 01 answered: the program stops with a fault.

## 2. The program

`examples/ledger.ci` has 96 lines. The top of the file declares the data:

```ci
// ledger.ci: an exact bank ledger. Money is whole cents held in I64.
// Run it with:  cint run examples/ledger.ci
// Test it with: cint test examples/ledger.ci
// There is no floating point anywhere.

// The kinds of entry, as typed constants.
const I64 DEPOSIT = 0;
const I64 WITHDRAW = 1;
const I64 INTEREST = 2;

struct Entry {
    I64 kind;
    I64 cents;    // the amount; for INTEREST, the yearly rate in basis points
}
```

A `const` has a declared type, and its value is fixed when the program is compiled (SPEC-04 5.2, LS-111). A `struct` is a value with named fields, like a Python dataclass with fixed field types; `Entry(DEPOSIT, 125_000)` builds one, with the fields in declaration order (SPEC-04 LS-77). The underscores in `125_000` are digit separators, as in Python.

### Interest, rounded by a stated rule

```ci
// One day of interest, rounded half to even, to the cent: the exact
// quotient of balance * basis_points by 10_000 * 365. muldiv forms the
// product wider than 64 bits and rounds once; only a result outside I64
// is a fault.
I64 daily_interest(I64 balance, I64 basis_points) {
    return muldiv(I64, balance, basis_points, 3_650_000, half_even);
}
```

A rate of 450 basis points is 4.50 % a year, and one day is 1/365 of a year, so one day's interest on `balance` cents is `balance * 450 / 3_650_000` cents. That quotient is rarely a whole number of cents, so the program has to round, and it says how: half to even, the rule Python's `round()` uses for a tie [2].

`muldiv(R, a, b, c, mode)` is one of CINT's integer built-ins (SPEC-04 LS-145, SPEC-01 IM-51). It computes `a * b / c` exactly, with an intermediate wider than 64 bits, rounds the quotient once by the named mode, and returns it as type `R`, here `I64`. Only that result is checked: a quotient that does not fit `R` faults `E_OVERFLOW`, and a zero divisor faults `E_DIV_ZERO`. With `half_even`, as here, it is `round(Fraction(a * b, c))` in Python terms, which also rounds a tie to even, followed by a check that the result fits 64 bits.

Until the integer built-ins compiled (roadmap box 07), this function multiplied in `I64` and rounded by hand with `/` and `%`. That product overflowed, as a fault, for balances above about 2 * 10^16 cents at 4.50 %. `muldiv` has no such limit: one day at 4.50 % on 10^17 cents is 12,328,767,123,288 cents. Reported: on 2026-10-05, the hand-written version and the `muldiv` line gave the same result for 240,000 pairs of a balance (0 to 18,259,087 cents, in steps of 913) and a rate (0 to 550 basis points, in steps of 50), and for the two ties of section 4, under `cint_ref` and under `cint run` on the GCC and Clang legs (a scratch program, not committed).

### An overdraft is an outcome, an overflow is a fault

```ci
// Overdrawing is an expected outcome: the entry is rejected.
Bool overdraws(I64 balance, Entry e) {
    return e.kind == WITHDRAW && e.cents > balance;
}

// Apply one entry. Overflow is a fault.
I64 apply(I64 balance, Entry e) {
    switch (e.kind) {
        case DEPOSIT:
            return balance + e.cents;
        case WITHDRAW:
            return balance - e.cents;
        default:
            return balance + daily_interest(balance, e.cents);
    }
}
```

A withdrawal larger than the balance is something a bank expects, so the program asks first (`overdraws`) and rejects the entry. A balance that does not fit an `I64` is not something the program can handle, so `balance + e.cents` stays a checked addition: if it ever overflows, the program stops with `E_OVERFLOW` and its fault record. A `switch` in CINT does not fall through from one `case` to the next (SPEC-04 LS-178), and a `switch` on an integer needs a `default` unless its cases cover every value of the type (LS-182). The `default` arm handles `INTEREST`.

### Printing exact dollars

```ci
// Print cents as exact dollars: 84950 prints as 849.50. The scale /100
// divides by 100 exactly and prints two digits after the point.
void print_dollars(I64 cents) {
    "{cents:/100}";
}
```

A hole in a print statement can hold any integer expression, and the text after a colon is a format specification, in a syntax adapted from Python's (SPEC-04 9.3, LS-202). CINT adds a scale suffix: `/100` prints the value divided by 100 as a decimal with exactly two digits after the point. For a balance of 84950 cents, `{cents:/100}` prints `849.50`, and `{-5:/100}` prints `-0.05` (SPEC-04 LS-206). A scale is a power of ten, so the printed decimal is exact: nothing is rounded.

### `main`

```ci
void main() {
    Entry[5] book;
    book[0] = Entry(DEPOSIT, 125_000);
    book[1] = Entry(WITHDRAW, 40_050);
    book[2] = Entry(INTEREST, 450);      // 4.50 % a year, for one day
    book[3] = Entry(WITHDRAW, 200_000);  // more than the balance: rejected
    book[4] = Entry(DEPOSIT, 99);

    I64 balance = 0;
    for i in 0..5 {
        print_entry(i, book[i]);
        if (overdraws(balance, book[i])) {
            " rejected: overdraws\n";
            continue;
        }
        balance = apply(balance, book[i]);
        " -> ";
        print_dollars(balance);
        "\n";
    }
    "final balance: ";
    print_dollars(balance);
    "\n";
}
```

The statements live in `void main() { ... }`, not at the top level of the file, because a top-level statement, even a print, makes the file a script, and a script's variables are not visible inside its functions or tests (SPEC-04 LS-218, LS-219; C3004). `Entry[5] book;` is an array of five structs, zero-filled until assigned (SPEC-04 5.1). `for i in 0..5` runs `i` from 0 to 4, like Python's `range(5)` (SPEC-04 LS-171). `print_entry`, in lines 47 to 56 of the file, prints the index, the kind padded to eight characters, and the amount right-aligned in eight characters with `{e.cents:>8}`, as `f"{x:>8}"` does in Python.

### Copies and views

`book[i]` goes to `overdraws` and `apply` as an `in` parameter, the default mode: read-only for the call, and whether the struct is copied or passed by reference cannot be observed (SPEC-04 LS-119, LS-122). Elsewhere, `=` and the loops have fixed rules, and they differ from C# and NumPy:

| Code | In CINT | Clause | In C# or NumPy |
| --- | --- | --- | --- |
| `b = a;` for two structs | copies the whole struct | SPEC-04 LS-78 | C#: the same for a `struct`; a `class` copies the reference |
| `b = a;` for two arrays of the same shape | copies every element | LS-70 | C#: copies the reference, so both names see one array; NumPy: `b = a` binds a second name to one array |
| `I32[2] b = a[0..2];` | copies the elements into new storage | LS-69 | NumPy: a basic slice is a view, so a write to `b` reaches `a` |
| `t = s;` for two `Str` | rebinds `t` to the bytes of `s`; no byte is copied | LS-107 | C#: a `string` assignment copies the reference |
| `for x in v { ... }` | `x` is a copy of the element, read at the start of each iteration, and read-only | LS-176, LS-173 | C#: a `foreach` variable is read-only too |
| an `inout` parameter | a writable view; writes reach the caller | LS-119 | C#: a `ref` parameter |

Reported: the struct and array rows ran with `cint_ref` and `cint run` (GCC leg), 2026-10-05: after `b = a;` and a write to `b`, `a` kept its value in both. The slice and `for x in` rows are Specified only: neither `cint_ref` nor `cint run` compiles them yet (C9102). The NumPy view ran with NumPy 2.4.6 the same day. The C# column is from the language as commonly documented, not run.

## 3. Run it

```text
$ cint run --receipt results/cint/t2/demos/ledger/run-ledger-gcc.json examples/ledger.ci
0: deposit    125000 -> 1250.00
1: withdraw    40050 -> 849.50
2: interest      450 -> 849.60
3: withdraw   200000 rejected: overdraws
4: deposit        99 -> 850.59
final balance: 850.59
receipt: results/cint/t2/demos/ledger/run-ledger-gcc.json
```

The exit status was 0. The six program lines are stdout, 188 bytes (SHA-256 `006e3c8f...5493`), byte for byte the content of `examples/ledger.stdout`, which `cint_ref run --stdout` wrote. The check compares bytes, not lines:

```text
$ python cli/tests/compare_bytes.py --expect examples/ledger.stdout -- cint run examples/ledger.ci
identical: 188 bytes
```

Line 2 is the interest: 84950 cents at 450 basis points for one day is 38,227,500 / 3,650,000 = 10.47 cents, which rounds to 10, so 849.50 becomes 849.60. Line 3 is the rejected withdrawal: the balance stays 849.60, and the next deposit of 99 cents gives 850.59.

Measured: `results/cint/t2/demos/ledger/run-ledger-gcc.json` and `run-ledger-clang.json`, 2026-10-05, source revision `f2b4e10`. Both record outcome `value`, exit status 0, the stdout digest above, and `fuel_consumed` 26, equal to `cint_ref`'s `fuel-consumed 26`: one unit for `main`, one for each of the five loop iterations, and one for each of the 20 calls of user functions (SPEC-01 10.2). `python tools/cint_receipts.py identity results/cint/t2/demos/ledger` ends with `1 receipt identity in 2 receipts`: GCC and Clang computed the same result from the same generated source.

## 4. The tests

The last 14 lines of the file are three tests:

```ci
test "interest ties round to even" {
    assert(daily_interest(1_825_000, 1) == 0);   // exactly half a cent: 0
    assert(daily_interest(5_475_000, 1) == 2);   // exactly 1.5 cents: 2
    assert(daily_interest(84_950, 450) == 10);   // 10.47 cents: 10
}

test "an overdraft is rejected" {
    assert(overdraws(100, Entry(WITHDRAW, 101)));
    assert(!overdraws(100, Entry(WITHDRAW, 100)));
}

test "overflow is a fault" expect_fault E_OVERFLOW {
    _ = apply(I64.max, Entry(DEPOSIT, 1));
}
```

Tests sit next to the code, and `cint run` never runs them (SPEC-04 LS-232). A test passes when its body completes. `assert` stops the test with a fault when its condition is false. A test marked `expect_fault E_OVERFLOW` passes only if its body faults with exactly that code (SPEC-04 LS-235, LS-236); here `I64.max + 1` inside `apply` is the fault the test expects. `_ = ...` evaluates a call and discards its value.

The ties are the reason for the first test. 1,825,000 cents at 1 basis point is 1,825,000 / 3,650,000 = 0.5 cents, exactly half, so half to even gives 0. 5,475,000 cents is 1.5 cents, and half to even gives 2.

`cint test` runs the three tests, each in a fresh context (SPEC-06 3.5):

```text
$ cint test examples/ledger.ci
test ledger.ci:83 "interest ties round to even" ... ok
test ledger.ci:89 "an overdraft is rejected" ... ok
test ledger.ci:94 "overflow is a fault" ... ok
3 of 3 tests passed
```

The reference implementation runs one test at a time, from the `ref/` directory, and prints the outcome with its fault record:

```text
$ python -m cint_ref run --test "overflow is a fault" ../examples/ledger.ci
case ledger
source reference
outcome fault
stdout-bytes 0
fuel-consumed 2
fault.code E_OVERFLOW
fault.operation add.checked.i64
fault.operand I64 9223372036854775807
fault.operand I64 1
fault.exact 9223372036854775808
fault.limit I64 9223372036854775807
fault.position ledger.ci:33:28
fault.revision self
fault.address none
fault.stack-depth 1
```

The body faulted with `E_OVERFLOW`, so this test passes. The position 33:28 is the `+` in `balance + e.cents`, inside `apply`, one call deep. The other two tests give `outcome value`: their bodies complete, so they pass. Reported: these three runs, `cint_ref` at commit `f2b4e10`, 2026-10-05; no receipt file. `cint test` ran on the GCC and Clang legs the same day.

## 5. How this ledger differs from the one in SPEC-04

SPEC-04 section 19 (LS-308) has a ledger tutorial of its own, written for the full language. This example is written for the part of the language that slice 2 compiles. The differences:

| SPEC-04 section 19 | `examples/ledger.ci` | Why |
| --- | --- | --- |
| Top-level statements, a script | `void main() { ... }` | `balance` and `book` are used across a loop that calls functions, and the file has tests; the D-22 rule in section 2 above. |
| `enum Kind : U8 { deposit, withdraw, interest }` | three `const I64` values | Enums arrive at roadmap stage T4. Without an enum, `switch` needs a `default` (LS-182). |
| `error LedgerError { overdrawn }`, `LedgerError!I64`, `catch` | `overdraws` is asked before `apply` | Error sets and error unions arrive at T4 or later. |
| `I128` product and `div_round(..., half_even)` | `muldiv(I64, ..., half_even)` | Wide integers arrive at T5. `muldiv` keeps its wider product inside the call, so the program needs no `I128` (section 2). |
| `{e.kind:<8}` | each kind's name spelled out in `print_entry`, padded to eight characters | `:<8` pads an enum's name (LS-212); a `const I64` kind has no name to print. |
| An array literal `Entry[5] book = [ ... ];` | five element assignments | `cint_ref` does not implement array literals yet. |
| `for i, e in book` | `for i in 0..5` and `book[i]` | `cint_ref` does not implement the index-and-element form over an array yet. |
| `3: rejected LedgerError.overdrawn` | `3: withdraw   200000 rejected: overdraws` | Without error values, the program prints its own message. |

The arithmetic is the same, and so is the layout: five of the six lines that SPEC-04 LS-309 prints are byte for byte lines of this program's output. Only line 3, the rejected withdrawal, differs, for the reason in the table.

## 6. What this tutorial does not show

- The MSVC leg. The receipts above are from GCC and Clang on Linux.
- Nothing here is a performance claim. The receipts record build and run times as observations only.

## References

1. [PSF-Tutorial-FloatingPoint] Python Software Foundation. "Floating-Point Arithmetic: Issues and Limitations." *The Python Tutorial*, Python 3.14, chapter 15. https://docs.python.org/3/tutorial/floatingpoint.html. Read 2026-10-05.
2. [PSF-Functions] Python Software Foundation. "Built-in Functions": `round()`. *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/functions.html#round. The entry says that when two multiples are equally close, rounding is done toward the even choice. Cited from the documentation as known on 2026-10-05; not re-read for this tutorial.
