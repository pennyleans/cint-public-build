"""Compares the lean call path with cint_ref at every fuel allowance a short program can reach.

    python compiler/tests/call_protocol.py --exe CINT --out DIR [--leg msvc|gcc|clang]

CINT is the `cint` command that tools/cint_bootstrap.py builds with B1. Each program is run by
`cint_ref run --format 2` and compiled by `cint emit-c` and the native harness, and must give
the same outcome, return, fuel and fault lines (named_constructors.main), which at format 2
hold the fault's position and every site of its stack. The cases are a fault of each kind
raised 1, 2 and 3 calls deep, in a leaf and in a function that has made a call; E_DEPTH by
direct and mutual recursion, at the limit and one past it; E_FUEL at a call, at a charge inside
a loop in a callee and at the first charge after a return (the allowances of `fuels`); a
fault two deep through two frames that count; a function that prints, with its count in a
local; and the shapes that keep a function heavy. Then each program's emitted C must hold the
forms listed in FORMS, and a program with an extern must keep the protocol around it (HOST).
"""
import sys

import named_constructors as runner
import proven_checks

LIBRARY = 'export I64 one() { return 1; }\n'

# What the deepest function computes from x, which is 1 at run time, on the view v of [1, 2, 3, 4].
KINDS = {
    'overflow': 'x + 9223372036854775807',
    'bounds': 'v[x + 3]',
    'div_zero': 'x / (x - 1)',
    'shift': 'x << (x + 99)',
    'domain': 'isqrt(x - 2)',
}


def chain(expr, depth, leaf):
    """Function f1 faults with expr, in a leaf or after a call of its own, and f2 and f3 call down."""
    text = 'I64 id(I64 x) { return x; }\n'
    if leaf:
        text += 'I64 f1(in I64[_] v, I64 x) { return %s; }\n' % expr
    else:
        text += 'I64 f1(in I64[_] v, I64 y) { I64 x = id(y); return %s; }\n' % expr
    for k in (2, 3):
        text += 'I64 f%d(in I64[_] v, I64 x) { I64 y = id(x); return f%d(v, y) + 1; }\n' % (k, k - 1)
    return text + ('export I64 run() { I64[4] a = [1, 2, 3, 4]; I64 t = id(1); return f%d(a, t) + 2; }' % depth)


VALUES = {'%s_%s_%d' % (kind, 'leaf' if leaf else 'call', depth): chain(expr, depth, leaf)
          for kind, expr in KINDS.items() for depth in (1, 2, 3) for leaf in (True, False)}

DIRECT = 'I64 down(I64 n) { if (n == 0) { return 0; } return down(n - 1) + 1; }\n'
MUTUAL = ('I64 even(I64 n) { if (n == 0) { return 0; } return odd(n - 1) + 1; }\n'
          'I64 odd(I64 n) { if (n == 0) { return 1; } return even(n - 1) + 1; }\n')
VALUES.update({
    # The entry is frame 1 and down(n) reaches frame n + 2, so D = 256 admits n = 254.
    'depth_direct_limit': DIRECT + 'export I64 run() { return down(254); }',
    'depth_direct_past': DIRECT + 'export I64 run() { return down(255); }',
    'depth_mutual_limit': MUTUAL + 'export I64 run() { return even(254); }',
    'depth_mutual_past': MUTUAL + 'export I64 run() { return even(255); }',
    'recursion_short': DIRECT + 'export I64 run() { return down(9); }',
    # Charges at calls, at a loop in a callee and after a return.
    'fuel_calls': 'I64 a(I64 x) { return x + 1; }\nI64 b(I64 x) { return a(x) + a(x + 1); }\n'
                  'I64 c(I64 x) { return b(x) + a(x) + b(x); }\nexport I64 run() { return c(1) + a(2); }',
    'fuel_callee_loop': 'I64 total(I64 n) { I64 t = 0; I64 i = 0; while (i < n) { if (i % 3 == 0) { t = t + i; } '
                        'i = i + 1; } return t; }\nI64 twice(I64 n) { return total(n) + total(n + 1); }\n'
                        'export I64 run() { return twice(5) + total(3); }',
    'fuel_counted_callee': 'I64 total(I64 n) { I64 t = 0; for (I64 i = 0; i < n; i++) { t = t + i; } return t; }\n'
                           'I64 twice(I64 n) { return total(n) + total(n + 1); }\n'
                           'export I64 run() { return twice(5) + total(3); }',
    # A fault two deep through two frames that count, each with a loop.
    'two_counting_frames': 'I64 inner(I64 n) { I64 t = 9223372036854775800; I64 i = 0; while (i < n) { '
                           'if (i % 2 == 0) { t = t + i; } i = i + 1; } return t; }\n'
                           'I64 outer(I64 n) { I64 t = 0; I64 i = 1; while (i < n) { t = inner(i) / 2 + i; '
                           'i = i + 1; } return t; }\nexport I64 run() { return outer(12); }',
    'two_counting_counted': 'I64 inner(I64 n) { I64 t = 9223372036854775800; for (I64 i = 0; i < n; i++) { '
                            't = t + i; } return t; }\nI64 outer(I64 n) { I64 t = 0; I64 i = 1; while (i < n) '
                            '{ t = inner(i) / 2 + i; i = i + 1; } return t; }\nexport I64 run() { return outer(12); }',
    # A lean function that prints: the count reaches the head before the line ends.
    'print_leaf': 'I64 say(I64 x) { "x {x}\\n"; return x + 1; }\nI64 mid(I64 x) { return say(x) + say(x + 1); }\n'
                  'export I64 run() { return mid(1) + mid(2); }',
    'print_then_fault': 'I64 say(I64 x) { "x {x}\\n"; return x / (x - 2); }\n'
                        'I64 mid(I64 x) { return say(x) + say(x + 1); }\nexport I64 run() { return mid(1); }',
    # Module state from a lean function that counts.
    'state_counts': 'I64 g = 5;\nI64 bump(I64 x) { I64 i = 0; while (i < x) { g = g + x; i = i + 1; } return g; }\n'
                    'I64 mid(I64 x) { return bump(x) + bump(1); }\nexport I64 run() { return mid(2); }',
    'state_faults': 'I64 g = 9223372036854775800;\nI64 bump(I64 x) { g = g + x; return g; }\n'
                    'I64 mid(I64 x) { I64 a = bump(x); return a + bump(9); }\nexport I64 run() { return mid(2); }',
    # Hidden parameters beside the view, struct and result ones.
    'views': 'I64 get(in I64[_] v, I64 i) { return v[i]; }\nvoid put(inout I64[_] v, I64 i, I64 x) { v[i] = x; }\n'
             'void trade(inout I64[_] v, I64 i, I64 j) { I64 t = get(v, i); put(v, i, get(v, j)); put(v, j, t); }\n'
             'export I64 run() { I64[4] a = [1, 2, 3, 4]; trade(a, 0, 3); return get(a, 0) * 10 + get(a, 3); }',
    'structs': 'struct P { I64 x; I64 y; }\nP add(P a, P b) { P r = P(a.x + b.x, a.y + b.y); return r; }\n'
               'P twice(P a) { return add(a, a); }\nexport I64 run() { P p = P(2, 3); P q = twice(p); '
               'return q.x * 10 + q.y; }',
    # A call nothing reaches, and functions that need neither the count nor the room.
    'dead_call': 'I64 a(I64 x) { return x; }\nI64 b(I64 x) { return x + 1; return a(x); }\n'
                 'export I64 run() { return b(1); }',
    'quiet': 'I64 k() { return 7; }\nvoid none() { }\nexport I64 run() { none(); return k(); }',
    # The shapes that keep a function heavy, around a lean one.
    'fixed_wrap': 'Q16.16 wrapped(Q16.16 a, Q16.16 b) { return a *% b; }\nI64 leaf(I64 x) { return x / (x - 1); }\n'
                  'I64 mid(I64 x) { Q16.16 q = wrapped(1.5, 2.5); if (q > 1.0) { return leaf(x); } return 0; }\n'
                  'export I64 run() { return mid(1); }',
    'other_module': 'I64 leaf(I64 x) { return x / (x - 1); }\nI64 mid(I64 x) { return records.one() + leaf(x); }\n'
                    'export I64 run() { return mid(1); }',
    'export_called': 'export I64 helper(I64 x) { return x / (x - 1); }\nI64 mid(I64 x) { return helper(x); }\n'
                     'export I64 run() { return mid(1); }',
    # A kernel body makes no call (SPEC-02 K-11), so no lean frame runs under a dispatch position;
    # a kernel's fault is format 4 (CONF-11 rule 13), which conformance/kernel covers.
})

UNWIND = 'cint_rt_lean_unwind(ctx, '
ENTER = 'cint_rt_call_enter(ctx, '
LEAN = 'cint_rt_lean_enter(ctx, '
INLINE = 'cint_rt_lean_fail(ctx, '
# Per program, what its emitted main.c must hold: `<n>=text` n times, `+text` at all, `-text` not.
FORMS = {
    # run -> lean_enter -> f1, with id called by run and by f1.
    'overflow_leaf_1': ['-' + ENTER, '-' + INLINE],
    'overflow_call_1': ['1=' + INLINE, '-' + ENTER],
    'overflow_call_3': ['-' + ENTER, '+' + INLINE, '+' + UNWIND],
    'depth_direct_past': ['-' + ENTER, '1=' + LEAN, '1=' + INLINE],
    'depth_mutual_past': ['-' + ENTER, '2=' + INLINE],
    'two_counting_frames': ['-' + ENTER, '+, int64_t fu', '+return -1;'],
    'two_counting_counted': ['-' + ENTER, '1=_hq:;'],
    'print_leaf': ['-' + ENTER, '+fh->fuel_used = fu;'],
    'state_counts': ['-' + ENTER, '+cint_rt_head_of(ctx)->fuel_used = fu;'],
    'quiet': ['-' + ENTER, '-' + INLINE, '-fcap'],
    # heavy: a fixed-point `%` form runs on after a refusal (CINTC-OQ-76 item 4)
    'fixed_wrap': ['2=' + ENTER, '1=' + LEAN],
    # heavy: an import row has no SIR, so its caller keeps the protocol
    'other_module': ['2=' + ENTER, '1=' + LEAN],
    'export_called': ['2=' + ENTER],
}

HOST = '''extern observe I64 clock();
I64 leaf(I64 x) { return x + 1; }
I64 mid(I64 x) { return leaf(clock()) + x; }
I64 top(I64 x) { return mid(x); }
I64 pure(I64 x) { return leaf(x); }
export I64 run() { return top(1) + pure(2); }
'''


def fuels(consumed):
    """Allowances from none to one past what the program uses, the first 40 of a long program."""
    return sorted(set(range(0, min(consumed, 40) + 1)) | {consumed - 1, consumed, consumed + 1} - {-1})


def host_forms(exe, out):
    """A host call runs under heavy frames only: mid and top keep the protocol, pure does not."""
    directory = out / 'host'
    directory.mkdir()
    (directory / 'main.ci').write_bytes(HOST.encode('ascii'))
    result = runner.subprocess.run([str(exe), 'emit-c', '--root', '.', '--out', 'out', 'main.ci'], cwd=directory,
                                   capture_output=True, text=True, timeout=120)
    files = list(directory.glob('out*/main.c'))
    if result.returncode != 0 or len(files) != 1:
        return ['host: emit-c failed: ' + result.stdout + result.stderr]
    text = files[0].read_text(encoding='ascii')
    wants = ((text.count(ENTER), 2, 'run and top call their heavy callees through the protocol'),
             (text.count(LEAN), 2, 'mid and run call their lean callees through cint_rt_lean_enter'),
             (text.count(INLINE), 1, 'pure checks its call inline'))
    return ['host: %s: %d, expected %d' % (what, got, want) for got, want, what in wants if got != want]


if __name__ == '__main__':
    status = runner.main(VALUES, {}, {}, library=LIBRARY, label='call protocol', fuels=fuels)
    out = runner.pathlib.Path(sys.argv[sys.argv.index('--out') + 1])
    names = [sys.argv[j + 1] for j, arg in enumerate(sys.argv) if arg == '--only'] or list(FORMS)
    names = [name for name in names if name in FORMS]
    broken = proven_checks.forms(out, names, FORMS)
    exe = runner.pathlib.Path(sys.argv[sys.argv.index('--exe') + 1])
    broken += host_forms(exe, out)
    for line in broken:
        print('form: ' + line)
    print('call protocol forms: %d of %d programs as written' % (len(names) + 1 - len({b.split(':')[0] for b in broken}),
                                                                   len(names) + 1))
    raise SystemExit(status or (1 if broken else 0))
