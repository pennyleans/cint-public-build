<img src="../../assets/brand/cint-logo.svg" alt="CINT" width="96">

# Tutorial 01: hello, and your first overflow

Status: tutorial, milestone M1 of slice 2, 2026-10-03. Written for a Python developer meeting CINT for the first time. Every output below is pasted from a run on Windows with the MSVC leg; where a run has a receipt, the receipt is named.

In this tutorial you build the `cint` command, run a two-line program, and then watch CINT stop a program whose number does not fit. You will read the fault record that CINT prints, field by field.

## 1. Build `cint`

From the repository root, on Windows:

```text
python tools/cint_bootstrap.py --leg msvc
```

This builds the seed compiler, uses it to build `cintc` (the CINT compiler, which is itself written in CINT), and puts the `cint` command at `<build>\boot\msvc\cint.exe`. `<build>` is the directory that the `CINT_BUILD` environment variable names, or `cint-build` in the system temporary directory (`%TEMP%\cint-build`) when `CINT_BUILD` is not set; build outputs never go inside the repository. On Linux, `--leg gcc` and `--leg clang` build `<build>/boot/<leg>/cint`. `cint run` turns your program into C17, compiles it with the host C compiler, and runs it.

## 2. The program

`examples/hello.ci` has three lines:

```ci
"hello, world\n";
I64 answer = 6 * 7;
"answer={answer}\n";
```

If you know Python, most of this reads the way you expect:

| Python | CINT | Note |
| --- | --- | --- |
| `print("hello, world")` | `"hello, world\n";` | A string on its own, ending in `;`, prints. There is no automatic newline, so you write `\n` (SPEC-04 LS-193). |
| `answer = 6 * 7` | `I64 answer = 6 * 7;` | Every variable has a type, written first. `I64` is a signed 64-bit integer. |
| `print(f"answer={answer}")` | `"answer={answer}\n";` | Holes in braces work like an f-string (SPEC-04 section 2). |

The big difference is the type. A Python `int` has no fixed size; it grows as large as memory allows [1]. A CINT `I64` holds exactly the values from -9223372036854775808 to 9223372036854775807, and every `+`, `-`, and `*` on it is checked. Section 4 shows what that means.

## 3. Run it

This is the run that wrote the M1 receipt on Windows:

```text
> <build>\boot\msvc\cint.exe run --receipt results/cint/t2/m1/run-hello-msvc.json examples/hello.ci
hello, world
answer=42
receipt: results/cint/t2/m1/run-hello-msvc.json
```

The exit status was 0. The two program lines are stdout, exactly 23 bytes with LF line ends (SHA-256 `0d3f756a...4b57`), the same bytes as `examples/hello.stdout`. The `receipt:` line is on stderr, not stdout: stdout carries only what your program prints, and everything `cint` itself says goes to stderr (SPEC-06 3.4a). Leave out `--receipt` and stderr is empty. Add `--json` and stderr is empty too, because a JSON caller already knows where it asked the receipt to go.

Measured: `results/cint/t2/m1/run-hello-msvc.json`, `run-hello-gcc.json`, and `run-hello-clang.json`, 2026-10-03, source revision `d005372`. All three record outcome `value`, exit status 0, the same stdout digest, `fuel_consumed` 1, and the same generated C. `python tools/cint_receipts.py identity results/cint/t2/m1` ends with `1 receipt identity in 3 receipts`: MSVC on Windows, GCC and Clang on Linux computed the same result from the same generated source.

## 4. Scripts and `void main()`

`hello.ci` is a script: a file with statements at the top level, which run in order, like a notebook cell (SPEC-04 LS-218). That is fine for a file with no functions. As soon as a program has functions, write its statements inside `void main() { ... }` instead, because a top-level statement, even a print, makes the file a script, and a script's variables are not visible inside its functions or tests (SPEC-04 LS-218, LS-219; C3004). The same program in that form:

```ci
void main() {
    "hello, world\n";
    I64 answer = 6 * 7;
    "answer={answer}\n";
}
```

Reported: this form was run once with `cint run --json` on MSVC, 2026-10-03, at `cc59359`; it printed the same 23 bytes and exited 0 (log and receipt outside the repository). The examples in later tutorials that declare functions use this form.

## 5. When the number does not fit

`examples/hello_overflow.ci` counts one past the largest `I64`:

```ci
"counting past the largest I64\n";
I64 big = 9223372036854775807;
"big={big}\n";
I64 next = big + 1;
"next={next}\n";
```

In Python, `big + 1` is 9223372036854775808 and the program goes on [1]. NumPy's guide warns that its fixed-size integer types overflow [2]: in an `int64` array, `big + 1` wraps around to -9223372036854775808 with no error. In CINT, `big + 1` has no `I64` answer, so the program stops with a fault:

```text
> <build>\boot\msvc\cint.exe run --receipt results/cint/t2/m1-overflow/run-hello_overflow-msvc.json examples/hello_overflow.ci
counting past the largest I64
big=9223372036854775807
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
receipt: results/cint/t2/m1-overflow/run-hello_overflow-msvc.json
```

The exit status was 1. The first two lines are stdout: the 54 bytes the program printed before the fault, kept and flushed before the fault report. Line 5 never ran, so `next=` was never printed. Everything from `fault[E_OVERFLOW]` on is stderr. With `--json`, stderr instead holds exactly one line, a `CINT-FAULT-1` JSON object with the same fields, and no `receipt:` line (SPEC-06 3.4a).

Measured: `results/cint/t2/m1-overflow/run-hello_overflow-msvc.json`, 2026-10-03, source revision `cc59359`, records outcome `fault`, exit status 1, the stdout digest (54 bytes, SHA-256 `a2ed99bd...4237`), every field of the fault record above, and `fuel_consumed` 1. The receipt stores the record, not the stderr text; the text above is pasted from the same run. Reported: in the M1 integration check of 2026-10-03, the same program on all three legs gave stdout bytes, exit status, every fault field, and fuel consumed equal to those of `cint_ref`, the reference implementation, and a `revision` equal to one assembled independently from the generated SIR.

## 6. Reading the fault record

Each field has one job (SPEC-01 IM-106, the canonical fields; SPEC-06 section 8, how tools show them). `code` says what kind of fault it was: `E_OVERFLOW` means a checked operation's exact result was outside its type's range. `operation` names the exact operation in the form `<op>.<form>.<type>`: `add.checked.i64` is addition, in its checked form (the plain `+`), on `I64` (SPEC-04 LS-284). `operands` are the inputs, each with its type and its exact value: `big`, and the literal `1`, which took the type `I64` from `big` (SPEC-04 LS-56). `exact` is the true mathematical result, 9223372036854775808, the value that failed the range check. `limit` is the bound it crossed, `I64` 9223372036854775807. `position` is the file, line, and column of the operator token, here the `+` at line 4, column 16, not the start of the statement (SPEC-04 LS-278). `revision` is the revision identity of the program, a SHA-256 over the program's meaning rather than its formatting (SPEC-01 IM-157): adding a blank line does not change it. So the revision identifies the program that faulted, but not the text a `line:column` refers to, because a blank line moves every position below it. That is the job of the source map (`source-map`, below). `fuel-consumed` counts the fuel units the run used before it stopped (SPEC-01 IM-137, IM-139): fuel is a step budget, not time, and the script's one entry charged 1. `cint` prints it beside the record, marked "not part of the fault record", because fuel belongs to the run's result rather than to the fault (SPEC-01 IM-137).

The other lines describe the record's shape and what it leaves out. `operand-count` is the number of `operand` lines that follow, here 2; IM-106 records "Every operand as a typed value, exactly (never rounded or abbreviated), in source order, at most 8." `stack-depth` is the number of call-site positions in the record's `stack` field, which IM-106 defines as "Call-site positions of the active user-function calls, outermost first"; it is 0 here because the fault is in the script's own body, and the script's implicit entry is a host call with no call site (SPEC-01 IM-107). `address` is `none` because IM-106 makes it "Present only for faults of a kernel dispatch". `source-map` would name the source-map identity, the digest of the table from each site to its line and column (SPEC-06 section 1.3); `cint run` does not produce a source map at M1, so it prints `none`; until it does, the source file itself, or a receipt, which records the file's SHA-256, ties a position to the text that ran. `state` is not a field of the record: `cint` adds it to say what the program left behind, here that it stopped and that stdout holds what it wrote before the fault.

The labels above are the ones `cint run` prints, one line per canonical field under its SPEC-01 IM-106 name. This layout was chosen on 2026-10-03 (decision 26, which closes OQ-170 in `docs/cint/OPEN-QUESTIONS.md`), and the SPEC-04 LS-284 example now shows this same report. The SPEC-06 section 8.4 example still uses another layout (`left`, `right`, `range`, `called`, `help`) for the same content.

The same rules cover a shift: `<<` is checked and faults with `E_OVERFLOW` when the result does not fit its type (SPEC-04 LS-156), so `I32 x = 1 << 31;` does not fit `I32`. Because both operands are literals, it is evaluated when the program is compiled and reported as compile error C6001 carrying `E_OVERFLOW` (SPEC-04 LS-54).

## 7. What this tutorial does not show

- `cint run` at milestone M1 compiles part of the language, enough for these examples. Parity with the seed compiler and the full scalar surface come with slice 2 tasks 2.13 and 2.14.
- Nothing here is a performance claim. The receipts record build and run times as observations only.
- How to fix an overflow without losing the check is the subject of `examples/overflow_hunt.ci` and tutorial 03 (milestone M2).

## 8. If a C host calls the same code

A C program can call a module's exports through the C interface of SPEC-03 section 5 instead of `cint run`. Three facts from outside the sections a newcomer reads first answer the usual questions after a fault.

- Printed bytes reach the host one statement at a time. The runtime stages each print statement and hands its bytes to the context's output callback in one call when the statement ends, so a statement whose hole faults writes nothing (SPEC-04 LS-197; the `cint_output_fn` comment in `rt/cint_rt.h`, from contract `cint-rt-2`). A print statement that completed before the faulting statement has already been delivered when the entry returns `CINT_FAULT`.
- `cint_fault_get` and `cint_ctx_clear_fault` each return a `cint_status` (SPEC-03 section 5.3 declares `cint_status cint_fault_get(const cint_ctx *ctx, uint8_t *buf, size_t cap, size_t *len);` and `cint_status cint_ctx_clear_fault(cint_ctx *ctx);`). On success each returns `CINT_OK` (0): SPEC-03 A-4 says "Every ABI function that can fail returns a `cint_status`. Results are written through out parameters only when the status is `CINT_OK`."
- The recovery order is fixed: read the record with `cint_fault_get`, clear it with `cint_ctx_clear_fault`, then call an entry that reinitializes the state. An entry called before the clear returns `CINT_FAULTED` (4) and runs nothing (SPEC-03 A-7, A-7b).

## References

1. [PSF-DataModel] Python Software Foundation. "The standard type hierarchy: numbers.Integral." *The Python Language Reference*, Python 3.14, section 3.2. https://docs.python.org/3/reference/datamodel.html. Read 2026-10-03.
2. [NumPy-Types] NumPy Developers. "Data types: Overflow errors." *NumPy User Guide*, NumPy 2.5. https://numpy.org/doc/stable/user/basics.types.html#overflow-errors. Read 2026-10-03.
