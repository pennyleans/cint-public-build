"""The inexact annotation of an entry's value (box 14; SPEC-06 5.2, section 5.2 of the box 14 note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, a transcript against B1: values of `div_round`, `muldiv`, and `isqrt_round` that
discarded a nonzero remainder, with the exact quotient in decimal or as a reduced fraction,
including a product of more than 64 bits; values these built-ins computed exactly; values that a
later operation or call produced; and the same calls by `:run`, on cpu-c17 alone (no annotation),
and with --check cpu-sir-interp.
"""
from __future__ import annotations

import pathlib
import tempfile

ENTRIES = """\
I64 subtotal = 2434;
I64 tax = div_round(subtotal * 825, 10000, half_even);
div_round(200 * 825, 10000, half_even)
div_round(200 * 825, 10000, half_up)
div_round(200 * 800, 10000, half_up)
div_round(-7, 2, half_even)
div_round(7, -3, floor)
div_round(div_round(7, 2, floor), 1, floor)
div_round(7, 2, floor) + 0
div_round(7, 2, floor) == 3
muldiv(I64, 3000000000000000001, 5, 8, floor)
muldiv(I64, 3000000000000000000, 5, 7, half_even)
isqrt_round(10, half_even)
isqrt_round(16, half_even)
U64 big = 18446744073709551615;
div_round(big, 10, half_even)
I32 small = div_round(-7, 4, ceil);
I64 id(I64 a) { return a; }
export I64 f(I64 a) { return div_round(a, 3, floor); }
export I64 g(I64 a) { I64 q = div_round(a, 3, floor); return id(q); }
f(7)
g(7)
:run f(10) 2
:backend cpu-c17
:run f(8)
:run f(8) --check cpu-sir-interp
:quit --yes
"""
SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 subtotal = 2434;
subtotal = 2434 : I64
ci[2]> I64 tax = div_round(subtotal * 825, 10000, half_even);
tax = 201 : I64   inexact: div_round half_even (exact quotient 200.805)
ci[3]> div_round(200 * 825, 10000, half_even)
= 16 : I64   inexact: div_round half_even (exact quotient 16.5)
ci[4]> div_round(200 * 825, 10000, half_up)
= 17 : I64   inexact: div_round half_up (exact quotient 16.5)
ci[5]> div_round(200 * 800, 10000, half_up)
= 16 : I64
ci[6]> div_round(-7, 2, half_even)
= -4 : I64   inexact: div_round half_even (exact quotient -3.5)
ci[7]> div_round(7, -3, floor)
= -3 : I64   inexact: div_round floor (exact quotient -7/3)
ci[8]> div_round(div_round(7, 2, floor), 1, floor)
= 3 : I64
ci[9]> div_round(7, 2, floor) + 0
= 3 : I64
ci[10]> div_round(7, 2, floor) == 3
= true : Bool
ci[11]> muldiv(I64, 3000000000000000001, 5, 8, floor)
= 1875000000000000000 : I64   inexact: muldiv floor (exact quotient 1875000000000000000.625)
ci[12]> muldiv(I64, 3000000000000000000, 5, 7, half_even)
= 2142857142857142857 : I64   inexact: muldiv half_even (exact quotient 15000000000000000000/7)
ci[13]> isqrt_round(10, half_even)
= 3 : I64   inexact: isqrt_round half_even
ci[14]> isqrt_round(16, half_even)
= 4 : I64
ci[15]> U64 big = 18446744073709551615;
big = 18446744073709551615 : U64
ci[16]> div_round(big, 10, half_even)
= 1844674407370955162 : U64   inexact: div_round half_even (exact quotient 1844674407370955161.5)
ci[17]> I32 small = div_round(-7, 4, ceil);
small = -1 : I32   inexact: div_round ceil (exact quotient -1.75)
ci[18]> I64 id(I64 a) { return a; }
id : (I64) -> I64   revision session@r5
ci[19]> export I64 f(I64 a) { return div_round(a, 3, floor); }
f : (I64) -> I64   revision session@r6
ci[20]> export I64 g(I64 a) { I64 q = div_round(a, 3, floor); return id(q); }
g : (I64) -> I64   revision session@r7
ci[21]> f(7)
= 2 : I64   inexact: div_round floor (exact quotient 7/3)
ci[22]> g(7)
= 2 : I64
ci[23]> :run f(10) 2
= 3 : I64   inexact: div_round floor (exact quotient 10/3)
= 3 : I64   inexact: div_round floor (exact quotient 10/3)
ci[25]> :backend cpu-c17
backend: cpu-c17; each revision is built as a library, entries on it are not recorded, and :inspect and the \
watches stay on cpu-sir-interp
ci[25]> :run f(8)
cpu-c17: revision <h> built as a library in <n> ms
= 2 : I64
ci[26]> :run f(8) --check cpu-sir-interp
= 2 : I64   inexact: div_round floor (exact quotient 8/3)
check: cpu-c17 and cpu-sir-interp agree at 1 boundary
ci[27]> :quit --yes
"""


def check(case, wb, exes) -> int:
    """The inexact annotation against B1."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp) / "inexact"
        (work / "root").mkdir(parents=True)
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo", "--out", work / "session"]
        result = wb.run(argv, cwd=work, entries=ENTRIES)
        case.assertClean(result, "inexact")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(SAID))
    return 1
