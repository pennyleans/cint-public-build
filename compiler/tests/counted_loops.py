"""Compares the counted loops that cintc writes twice, once without the per-iteration fuel
test (plan unit P-3(b); SPEC-09 EMIT-16, OQ-245), whose copy counts its charges at its exits
(P-3(c)), with cint_ref, at allowances around the fuel each program uses.

    python compiler/tests/counted_loops.py --exe CINT --out DIR [--leg msvc|gcc|clang]

CINT is the `cint` command that tools/cint_bootstrap.py builds with B1. Each program is run by
`cint_ref run --format 2` and compiled by `cint emit-c` and the native harness, and must give
the same outcome, return, fuel and fault lines (named_constructors.main), without an allowance
and with each allowance of FUELS, so that E_FUEL falls before, inside and after each loop, and
on its last iteration, and a fault falls inside a copy before and after the store to its
counter. Then each program's emitted C must hold the copies listed in FORMS: `_hq:;` labels the
head of a loop's copy, so a loop that is not counted has none, and `_fq:;` and `_gq:;` its
exits for a check that fails before and after that store.
"""
import sys

import named_constructors as runner
import proven_checks

LIBRARY = 'export I64 one() { return 1; }\n'

VALUES = {
    # the bound a parameter, a constant, a local
    'while_param': 'I64 total(I64 n) { I64 t = 0; I64 i = 0; while (i < n) { t = t + i % 97; i = i + 1; } return t; }\n'
                   'export I64 run() { return total(300) + total(0) + total(-4); }',
    'for_const': 'export I64 run() { I64 t = 0; for (I64 i = 0; i < 300; i++) { t = t * 3 % 1000003 + i; } return t; }',
    'local_bound': 'export I64 run() { I64 n = 250; I64 t = 0; I64 i = 0; while (i < n) { t += i; i += 1; } return t; }',
    # a negative start and a step of 3: fewer iterations than the guard allows for
    'step3': 'export I64 run() { I64 t = 0; for (I64 i = -50; i < 400; i += 3) { t = t + i; } return t; }',
    # no iteration
    'empty': 'export I64 run() { I64 t = 7; I64 i = 10; while (i < 5) { t = t + 1; i = i + 1; } return t; }',
    # the inner loop is counted at each entry, the outer one is not
    'nested': 'export I64 run() { I64 t = 0; for (I64 i = 0; i < 12; i++) { for (I64 j = 0; j < 25; j++) { '
              't = t + i * j; } } return t; }',
    'two_loops': 'export I64 run() { I64[64] a; I64 t = 0; for (I64 i = 0; i < 640; i++) { a[i % 64] += i; } '
                 'for (I64 k = 0; k < 64; k++) { t = t * 3 % 1000003 + a[k]; } return t; }',
    # faults inside a copy keep their record and fuel
    'overflow': 'export I64 run() { I64 t = 9223372036854775000; I64 i = 0; while (i < 300) { t = t + i; i = i + 1; } '
                'return t; }',
    'counter_overflow': 'export I64 run() { I64 t = 0; for (I64 i = 9223372036854775800; i < 9223372036854775807; '
                        'i += 5) { t = t + 1; } return t; }',
    'assert': 'export I64 run() { I64 t = 0; I64 i = 0; while (i < 300) { assert(i < 200); t = t + i; i = i + 1; } '
              'return t; }',
    # faults after the store to the counter, and with a step of 3 before and after it (P-3(c))
    'overflow_late': 'export I64 run() { I64 t = 9223372036854775000; I64 i = 0; while (i < 300) { i = i + 1; '
                     't = t + i; } return t; }',
    'assert_late': 'export I64 run() { I64 t = 0; I64 i = 0; while (i < 300) { i = i + 1; assert(i < 200); t = t + i; } '
                   'return t; }',
    'step3_overflow': 'export I64 run() { I64 t = 9223372036854775000; for (I64 i = -50; i < 400; i += 3) { t = t + 7; } '
                      'return t; }',
    'step3_late': 'export I64 run() { I64 t = 9223372036854775000; I64 i = 0; while (i < 900) { i = i + 3; t = t + 7; } '
                  'return t; }',
    # not counted
    'decreasing': 'export I64 run() { I64 t = 0; I64 i = 0; while (i > -50) { t = t * 7 % 1000 + i; i = i - 1; } '
                  'return t; }',
    'two_steps': 'export I64 run() { I64 t = 0; I64 i = 0; while (i < 100) { i = i + 1; t = t + i; i = i + 1; } '
                 'return t; }',
    'bound_stored': 'export I64 run() { I64 n = 100; I64 t = 0; I64 i = 0; while (i < n) { n = n - 1; t = t + i; '
                    'i = i + 1; } return t; }',
    'with_if': 'export I64 run() { I64 t = 0; I64 i = 0; while (i < 100) { if (i % 3 == 0) { t = t + i; } i = i + 1; } '
               'return t; }',
    'with_call': 'export I64 run() { I64 t = 0; I64 i = 0; while (i < 100) { t = t + records.one(); i = i + 1; } '
                 'return t; }',
    'unsigned': 'export I64 run() { U64 t = 0; U64 i = 0; while (i < 100) { t = t + i; i = i + 1; } return t as I64; }',
}

# Per program, the number of loop copies its emitted main.c must hold.
FORMS = {name: ['%d=_hq:;' % count] for name, count in (
    ('while_param', 1), ('for_const', 1), ('local_bound', 1), ('step3', 1), ('empty', 1), ('nested', 1),
    ('two_loops', 2), ('overflow', 1), ('counter_overflow', 1), ('assert', 1), ('overflow_late', 1),
    ('assert_late', 1), ('step3_overflow', 1), ('step3_late', 1), ('decreasing', 0), ('two_steps', 0),
    ('bound_stored', 0), ('with_if', 0), ('with_call', 0), ('unsigned', 0))}
# The copy's exits that set the fuel count (P-3(c)): a check before the store to the counter
# leaves by `_fq`, one after it by `_gq`, and a copy with no check has neither.
for name, before, after in (('while_param', 1, 0), ('for_const', 1, 0), ('step3', 1, 0), ('overflow', 1, 0),
                            ('overflow_late', 1, 1), ('assert_late', 1, 1), ('step3_overflow', 1, 0),
                            ('step3_late', 1, 1)):
    FORMS[name] += ['%d=_fq:;' % before, '%d=_gq:;' % after]


def fuels(consumed):
    """Allowances from none to one more than the program uses."""
    return sorted({f for f in (0, 1, 2, 3, consumed // 3, consumed // 2, consumed - 2, consumed - 1, consumed,
                               consumed + 1) if f >= 0})


if __name__ == '__main__':
    status = runner.main(VALUES, {}, {}, library=LIBRARY, label='counted loops', fuels=fuels)
    out = runner.pathlib.Path(sys.argv[sys.argv.index('--out') + 1])
    names = [sys.argv[j + 1] for j, arg in enumerate(sys.argv) if arg == '--only'] or list(FORMS)
    broken = proven_checks.forms(out, names, FORMS)
    for line in broken:
        print('form: ' + line)
    print('counted loops forms: %d of %d programs as written' % (len(names) - len({b.split(':')[0] for b in broken}),
                                                                   len(names)))
    raise SystemExit(status or (1 if broken else 0))
