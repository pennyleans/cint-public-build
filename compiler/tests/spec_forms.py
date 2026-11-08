"""Compares the cpu-c17-spec build (exact speculation; SPEC-09 BACK-17) with cint_ref: each program
runs its operation in a function of the speculation set, called from its entry, so the call is a
region, and its last call faults (or, for the `ok` programs, none does), after stores to module
variables and to a view argument that the restore must undo.

    python compiler/tests/spec_forms.py --exe CINT --out DIR [--leg msvc|gcc|clang]
        [--backend cpu-c17-spec [--spec-rerun]]

Without --backend the programs are compiled as cpu-c17 writes them, the control. With it, each
program is emitted with `cint emit-c --backend cpu-c17-spec` and must hold a region; with
--spec-rerun it is compiled with CINT_SPEC_FORCE_RERUN, so every region restores its snapshot
and runs the checked call. Every run, without an allowance and with each allowance from none to
one more than the program uses, must give cint_ref's outcome, return, fuel, fault and state
lines (named_constructors.main).
"""
import sys

import named_constructors as runner

LIBRARY = 'export I64 one() { return 1; }\n'

TYPES = {'I8': (8, True), 'U8': (8, False), 'I16': (16, True), 'U16': (16, False), 'I32': (32, True),
         'U32': (32, False), 'I64': (64, True), 'U64': (64, False)}


def lim(t):
    w, signed = TYPES[t]
    if signed:
        lo, hi = -(1 << (w - 1)), (1 << (w - 1)) - 1
    else:
        lo, hi = 0, (1 << w) - 1
    return lo, hi


def lit(v):
    # The least I64 has no literal of its own.
    return '(-9223372036854775807 - 1)' if v == -(1 << 63) else str(v)


# Per operator: (operand pairs that do not fault, the pair that faults), from the type's range.
def pairs(t, op):
    w, signed = TYPES[t]
    lo, hi = lim(t)
    if op == '+':
        return [(hi - 1, 1), (lo, 0) if not signed else (lo, hi)], (hi, 1)
    if op == '-':
        return [(lo + 1, 1), (hi, hi)], (lo, 1)
    if op == '*':
        return [(hi // 2, 2) if not signed else (hi // 2, 2), (lo, 1)], (hi, 2)
    if op == '/':
        ok = [(hi, 3), (lo, 1)] + ([(lo, 2), (-7, 2)] if signed else [])
        return ok, (lo, -1) if signed else (1, 0)
    if op == '%':
        ok = [(hi, 3)] + ([(lo, -1), (-7, 2)] if signed else [(lo, 1)])
        return ok, (1, 0)
    if op == '<<':
        return [(1, w - 2), (0, w - 1)], (hi, 1)
    if op == '<<%':
        return [(hi, 1), (1, w - 1)], (1, w)
    if op == '>>':
        return [(hi, w - 1), (lo, 1)], (1, -1) if signed else (1, w)
    raise ValueError(op)


def program(t, op, fault):
    ok, bad = pairs(t, op)
    calls = ok + ([bad] if fault else [])
    body = ['%s h = 0;' % t, 'I64 n = 0;',
            '%s f(%s a, %s b, inout I64[_] v) { n = n + 1; v[0] = v[0] + 1; h = a; return a %s b; }' % (t, t, t, op),
            'export I64 run() {', '    I64[2] v;']
    for a, b in calls:
        body.append('    h = f(%s, %s, v);' % (lit(a), lit(b)))
    body += ['    return n * 10 + v[0];', '}']
    return '\n'.join(body) + '\n'


VALUES = {}
for t in TYPES:
    for op, tag in (('+', 'add'), ('-', 'sub'), ('*', 'mul'), ('/', 'div'), ('%', 'rem'), ('<<', 'shl'),
                    ('<<%', 'shlw'), ('>>', 'shr')):
        for fault in (False, True):
            VALUES['%s_%s_%s' % (tag, t.lower(), 'fault' if fault else 'ok')] = program(t, op, fault)

VALUES.update({
    # negation: the least signed value, and any unsigned value but 0
    'neg_i32_fault': 'I64 n = 0;\nI32 f(I32 a) { n = n + 1; return -a; }\n'
                     'export I64 run() { I32 x = f(-2147483647); x = f(-2147483647 - 1); return n; }\n',
    'neg_u8_fault': 'I64 n = 0;\nU8 f(U8 a) { n = n + 1; return -a; }\n'
                    'export I64 run() { U8 x = f(0); x = f(3); return n; }\n',
    # narrowing, from every width
    'as_i64_i8_fault': 'I64 n = 0;\nI8 f(I64 a) { n = n + 1; return a as I8; }\n'
                       'export I64 run() { I8 x = f(-128); x = f(127); x = f(128); return n; }\n',
    'as_i32_u16_fault': 'I64 n = 0;\nU16 f(I32 a) { n = n + 1; return a as U16; }\n'
                        'export I64 run() { U16 x = f(65535); x = f(-1); return n; }\n',
    'as_u64_i64_fault': 'I64 n = 0;\nI64 f(U64 a) { n = n + 1; return a as I64; }\n'
                        'export I64 run() { I64 x = f(9223372036854775807); x = f(9223372036854775808); return n; }\n',
    'as_ok': 'I64 n = 0;\nI16 f(I64 a) { n = n + 1; return a as I16; }\n'
             'export I64 run() { I16 x = f(-32768); x = f(32767); return n + (x as I64); }\n',
    # isqrt on a negative operand
    'isqrt_fault': 'I64 n = 0;\nI64 f(I64 a) { n = n + 1; return isqrt(a); }\n'
                   'export I64 run() { I64 x = f(99); x = f(0); x = f(-1); return n; }\n',
    'isqrt_ok': 'I64 n = 0;\nI64 f(I64 a) { n = n + 1; return isqrt(a); }\n'
                'export I64 run() { return f(9223372036854775807) + f(15) + n; }\n',
    # index checks on a module array, a view argument and a local array
    'index_global_fault': 'I64[8] g;\nI64 f(I64 i) { g[0] = g[0] + 1; g[i] = 5; return g[i]; }\n'
                          'export I64 run() { I64 x = f(7); x = f(8); return x; }\n',
    'index_view_fault': 'void f(inout I64[_] v, I64 i) { v[0] = v[0] + 1; v[i] = 4; }\n'
                        'I64 n = 0;\nexport I64 run() { I64[3] v; f(v, 2); n = v[0]; f(v, -1); return n; }\n',
    'index_local_ok': 'I64 f(I64 i) { I64[4] a; a[i] = i; return a[i] + a[0]; }\n'
                      'export I64 run() { return f(3) + f(0); }\n',
    # loops: a counted loop, a loop that is not counted, a fault inside each
    'loop_counted_fault': 'I64 n = 0;\nI64 f(I64 k) { I64 t = 9223372036854775000; for (I64 i = 0; i < k; i++) { '
                          'n = n + 1; t = t + i; } return t; }\n'
                          'export I64 run() { I64 x = f(10); x = f(2000); return x; }\n',
    'loop_counted_ok': 'I64 f(I64 k) { I64 t = 0; for (I64 i = 0; i < k; i++) { t = t * 3 % 1000003 + i; } return t; }\n'
                       'export I64 run() { return f(500) + f(0) + f(-3); }\n',
    'loop_while_fault': 'I64 n = 0;\nI32 f(I32 k) { I32 t = 1; while (t > 0) { n = n + 1; t = t * k; } return t; }\n'
                        'export I64 run() { I32 x = f(-1); x = f(3); return n; }\n',
    # calls: nested twins, recursion to the depth limit, a struct value argument
    'nested_fault': 'I64 n = 0;\nI8 g(I8 a) { n = n + 1; return a + 100; }\nI8 f(I8 a) { return g(a) + g(a); }\n'
                    'export I64 run() { I8 x = f(1); x = f(20); return n; }\n',
    'recursion_depth_fault': 'I64 n = 0;\nI64 r(I64 k) { n = n + 1; if (k == 0) { return 0; } return r(k - 1) + 1; }\n'
                             'export I64 run() { I64 x = r(50); x = r(100000); return x; }\n',
    'recursion_ok': 'I64 r(I64 k) { if (k == 0) { return 0; } return r(k - 1) + 2; }\n'
                    'export I64 run() { return r(200); }\n',
    'struct_arg': 'struct P { I64 a; I64 b; }\nI64 n = 0;\nI64 f(P p) { n = n + 1; return p.a * p.b; }\n'
                  'export I64 run() { P p = P(3000000000, 3); I64 x = f(p); p = P(3000000000, 3000000000); '
                  'x = f(p); return x; }\n',
    'library_call': 'I64 f(I64 a) { return a + records.one(); }\n'
                    'export I64 run() { return f(9223372036854775806) + f(9223372036854775807); }\n',
})

# The programs that must not hold a region: a callee outside the speculation set.
NO_REGION = {'library_call'}


def regions(out, names):
    """The programs whose emitted C has no region (one `#if !defined(CINT_SPEC_FORCE_RERUN)` per
    commit and one per label), apart from NO_REGION."""
    missing = []
    for name in names:
        text = ''.join(p.read_text() for p in sorted((out / name).glob('*.c')))
        count = text.count('#if !defined(CINT_SPEC_FORCE_RERUN)')
        if (count == 0) != (name in NO_REGION):
            missing.append('%s: %d region lines' % (name, count))
    return missing


def fuels(consumed):
    """Allowances from none to one more than the program uses."""
    return sorted({f for f in (0, 1, 2, 3, consumed // 3, consumed // 2, consumed - 2, consumed - 1, consumed,
                               consumed + 1) if f >= 0})


if __name__ == '__main__':
    argv = sys.argv[1:]
    mode = False
    if '--backend' in argv:
        k = argv.index('--backend')
        if argv[k + 1:k + 2] != ['cpu-c17-spec']:
            raise SystemExit('--backend takes cpu-c17-spec')
        del argv[k:k + 2]
        runner.EMIT_ARGS[:] = ['--backend', 'cpu-c17-spec']
        mode = True
    if '--spec-rerun' in argv:
        if not mode:
            raise SystemExit('--spec-rerun is for --backend cpu-c17-spec')
        argv.remove('--spec-rerun')
        runner.COMPILE_DEFINES = ('CINT_SPEC_FORCE_RERUN',)
    sys.argv[1:] = argv
    status = runner.main(VALUES, {}, {}, library=LIBRARY, label='spec forms', fuels=fuels)
    if mode:
        out = runner.pathlib.Path(argv[argv.index('--out') + 1])
        names = [argv[j + 1] for j, arg in enumerate(argv) if arg == '--only'] or list(VALUES)
        missing = regions(out, names)
        for line in missing:
            print('region: ' + line)
        print('spec forms regions: %d of %d programs as expected' % (len(names) - len(missing), len(names)))
        status = status or (1 if missing else 0)
    raise SystemExit(status)
