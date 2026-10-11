"""Compares the remainders and index checks that cintc writes without a helper or a check
(plan unit P-2(b); SPEC-09 EMIT-16 and EMIT-18, OQ-245) with cint_ref.

    python compiler/tests/proven_checks.py --exe CINT --out DIR [--leg msvc|gcc|clang]

CINT is the `cint` command that tools/cint_bootstrap.py builds with B1. Each program is run by
`cint_ref run --format 2` and compiled by `cint emit-c` and the native harness, and must give
the same outcome, return, fuel and fault lines (named_constructors.main). Then each program's
emitted C must hold, or not hold, the forms of FORMS, so that a case cannot pass because the
emitter kept the helper: a remainder by a positive constant is a mask when the constant is a
power of two, C's % when the dividend is unsigned or a loop counter proven never negative,
and its helper otherwise; an index check is not written when its index is a remainder by a
positive constant no greater than the extent.
"""
import sys

import named_constructors as runner
from import_views import golden_suite

LIBRARY = 'export I64 one() { return 1; }\n'

VALUES = {
    # the loop counter of ledger.ci: stored 0 before the loop, then only i + 1
    'counter': 'export I64 run() { I64 t = 0; I64 i = 0; while (i < 300) { t += i % 97; i = i + 1; } return t; }',
    # the step part of a for loop, and a step of 3
    'for_step': 'export I64 run() { I64 t = 0; for (I64 i = 0; i < 300; i += 3) { t += i % 7; } return t; }',
    # a counter from a negative constant is not proven; a power of two is a mask for any dividend
    'negative_start': 'export I64 run() { I64 t = 0; I64 i = -300; while (i < 300) { '
                      't = (t * 31 + i % 7 + i % 16) % 1000003; i = i + 1; } return t; }',
    # a counter that decreases is not proven
    'decreasing': 'export I64 run() { I64 t = 0; I64 i = 0; while (i > -50) { t = t * 7 + i % 7; i = i - 1; } '
                  'return t; }',
    # another store to the counter inside the loop is not proven
    'reset': 'export I64 run() { I64 t = 0; I64 i = 0; I64 k = 0; while (k < 10) { t = t * 10 + i % 7; '
             'i = k - 5; k = k + 1; } return t; }',
    # an outer loop's counter is not proven, the inner one's is
    'nested': 'export I64 run() { I64 t = 0; for (I64 i = 0; i < 20; i++) { t += i % 3; '
              'for (I64 j = 0; j < 20; j++) { t += j % 5; } } return t; }',
    # unsigned dividends, and narrow types
    'unsigned': 'export I64 run() { U64 u = 18446744073709551615; U8 b = 250; U32 w = 4000000000; '
                'return (u % 10) as I64 * 1000000 + (b % 7) as I64 * 1000 + (w % 9) as I64; }',
    'narrow_mask': 'export I64 run() { I8 x = -7; I16 y = -32768; I32 z = -5; '
                   'return (x % 4) as I64 * 10000 + (y % 64) as I64 * 100 + (z % 8) as I64; }',
    'extremes': 'export I64 run() { I64 m = -9223372036854775807 - 1; I64 x = 9223372036854775807; '
                'return m % 64 + m % 1 + x % 4611686018427387904 % 1000 + (m + 5) % 4611686018427387904 % 1000; }',
    # an index that is a remainder by the extent or less needs no check
    'index_extent': 'export I64 run() { I64[256] a; for (I64 i = 0; i < 1000; i++) { a[i % 256] += 1; } '
                    'for (I64 i = -700; i < 0; i++) { a[i % 256] += 3; } return a[3] * 1000 + a[255]; }',
    'index_below': 'export I64 run() { I64[10] a; I64 t = 0; for (I64 i = -40; i < 40; i++) { a[i % 7] += i; } '
                   'for (I64 k = 0; k < 10; k++) { t = t * 3 + a[k]; } return t; }',
    # a remainder by more than the extent keeps the check, which faults
    'index_above': 'export I64 run() { I64[10] a; I64 t = 0; for (I64 i = 0; i < 40; i++) { t += a[i % 16]; } '
                   'return t; }',
    'field_index': 'struct R { I64[4] xs; I64 n; }\n'
                   'export I64 run() { R r; for (I64 i = -9; i < 9; i++) { r.xs[i % 4] += i; } '
                   'return r.xs[0] * 1000000 + r.xs[1] * 10000 + r.xs[2] * 100 + r.xs[3]; }',
    # a remainder by a value keeps the helper and its fault
    'divisor_zero': 'export I64 run() { I64 t = 0; I64 d = 3; for (I64 i = 0; i < 10; i++) { t += i % d; d = d - 1; } '
                    'return t; }',
    # a function whose only check is proven has no fault label, which C compilers warn on
    'no_label': 'export I64 low(I64 x) { return x % 16; }\n'
                'export I64 run() { return low(-37) * 100 + low(100); }',
    # a fault after a proven remainder keeps its record and fuel
    'overflow_after': 'export I64 run() { I64 t = 9223372036854775000; I64 i = 0; '
                      'while (i < 2000) { t = t + i % 97; i = i + 1; } return t; }',
}

# Per program, text its emitted main.c must hold (+), must not hold (-), or must hold N times (N=).
FORMS = {
    'counter': ['+ % t', '-cint_rem_i64'],
    'for_step': ['+ % t', '-cint_rem_i64'],
    'negative_start': ['+cint_rem_i64', '+ & ((uint64_t)'],
    'decreasing': ['+cint_rem_i64'],
    'reset': ['+cint_rem_i64'],
    'nested': ['+cint_rem_i64', '+ % t'],
    'unsigned': ['-cint_rem_u64', '-cint_rem_u8', '-cint_rem_u32'],
    'narrow_mask': ['-cint_rem_i8', '-cint_rem_i16', '-cint_rem_i32'],
    'extremes': ['4=& ((uint64_t)'],
    'index_extent': ['2=cint_index_check'],
    # the check of a[k] in the second loop, which is counted, so written in its copy too (EMIT-16)
    'index_below': ['2=cint_index_check', '+cint_rem_i64'],
    'index_above': ['+cint_index_check'],
    'field_index': ['4=cint_index_check'],
    'divisor_zero': ['+cint_rem_i64'],
    'no_label': ['-cint_rem_i64', '1=fault:'],
    'overflow_after': ['-cint_rem_i64', '+cint_add_i64'],
}


def forms(out, names, table=None):
    """Each line of table (FORMS) that the emitted C of out/<case> breaks, for the cases names."""
    bad = []
    for name in names:
        wants = (FORMS if table is None else table)[name]
        if not (out / name / 'out' / 'MANIFEST.ref').is_file():
            bad.append('%s: no emitted C' % name)
            continue
        files = golden_suite.output_set(out / name / 'out')
        text = files['main.c'].decode('ascii')
        for want in wants:
            count, _, what = want.partition('=') if want[0].isdigit() else ('', '', want[1:])
            if (text.count(what) != int(count)) if count else ((what in text) != (want[0] == '+')):
                bad.append('%s: %s' % (name, want))
    return bad


if __name__ == '__main__':
    status = runner.main(VALUES, {}, {}, library=LIBRARY, label='proven checks')
    out = runner.pathlib.Path(sys.argv[sys.argv.index('--out') + 1])
    names = [sys.argv[j + 1] for j, arg in enumerate(sys.argv) if arg == '--only'] or list(FORMS)
    broken = forms(out, names)
    for line in broken:
        print('form: ' + line)
    print('proven checks forms: %d of %d programs as written' % (len(names) - len({b.split(':')[0] for b in broken}),
                                                                   len(names)))
    raise SystemExit(status or (1 if broken else 0))
