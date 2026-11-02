"""Compares the conversions and right shifts that cintc writes without a helper (SPEC-09
EMIT-18) with cint_ref.

    python compiler/tests/plain_conversions.py --exe CINT --out DIR [--leg msvc|gcc|clang]

CINT is the `cint` command that tools/cint_bootstrap.py builds with B1. Each program is run by
`cint_ref run --format 2` and compiled by `cint emit-c` and the native harness, and must give
the same outcome, return, fuel and fault lines (named_constructors.main). Then each program's
emitted C must hold, or not hold, the forms of FORMS, so that a case cannot pass because the
emitter kept the helper. Every operand reaches its node through a parameter, so that constant
folding cannot remove the node before the emitter sees it.

Conversions: all 64 ordered pairs of the eight scalar integer types (the 8 identity pairs
included). A pair is plain when every value of the source lies in the target: signed to a type
at least as wide, unsigned to a strictly wider signed type, unsigned to a type at least as wide.
A plain pair has one program of the edge values of its source, and no helper in its C; every
other pair keeps `cint_as_<to>_from_<from>`, has one program of the edge values that fit (no
fault) and up to two programs, each with one value that does not fit (E_NARROW).

Right shifts: `>>` by a constant count 0 <= k < width is plain for each of the eight types;
a count of the width or more, a negative count, and a count that is a parameter keep
`cint_shr_<t>`, and the first two fault. `<<` keeps `cint_shl_<t>`. A function holding a host
call is not tested here: it keeps the helpers by plain_fn.
"""
import sys

import named_constructors as runner
from import_views import golden_suite

LIBRARY = 'export I64 one() { return 1; }\n'

TYPES = {'I8': (8, True), 'I16': (16, True), 'I32': (32, True), 'I64': (64, True),
         'U8': (8, False), 'U16': (16, False), 'U32': (32, False), 'U64': (64, False)}


def low(t):
    bits, signed = TYPES[t]
    return -(1 << (bits - 1)) if signed else 0


def high(t):
    bits, signed = TYPES[t]
    return (1 << (bits - 1)) - 1 if signed else (1 << bits) - 1


def lit(t, v):
    """Source text of the value v of type t."""
    if v == -(1 << 63):
        return '-9223372036854775807 - 1'
    return str(v)


def plain_pair(to, frm):
    """Whether every value of frm lies in to."""
    return low(to) <= low(frm) and high(frm) <= high(to)


def edges(t):
    """The edge values of t, in order, once each."""
    lo, hi = low(t), high(t)
    cands = [lo, lo + 1, -1, 0, 1, 7, hi // 2, hi - 1, hi]
    seen, out = set(), []
    for v in cands:
        if lo <= v <= hi and v not in seen:
            seen.add(v)
            out.append(v)
    return out


def fold(calls):
    """An I64 expression of the results of calls (each an I64 expression), by a fixed hash."""
    body = ''.join('t = (t * 31 + (%s) %% 1000003) %% 1000000007; ' % c for c in calls)
    return body


def conversion_programs():
    values, forms = {}, {}
    for to in TYPES:
        for frm in TYPES:
            tag = '%s_%s' % (to.lower(), frm.lower())
            helper = 'cint_as_%s_from_%s(' % (to.lower(), frm.lower())
            head = '%s cv(%s x) { return x as %s; }\n' % (to, frm, to)
            if plain_pair(to, frm):
                xs = edges(frm)
            else:
                xs = [v for v in edges(frm) if low(to) <= v <= high(to)]
            decl = ''.join('%s a%d = %s; ' % (frm, i, lit(frm, v)) for i, v in enumerate(xs))
            calls = ['cv(a%d) as I64' % i for i in range(len(xs))]
            values['pl_' + tag] = (head + 'export I64 run() { I64 t = 0; ' + decl + fold(calls) + 'return t; }')
            forms['pl_' + tag] = ['-' + helper] if plain_pair(to, frm) else ['+' + helper]
            if plain_pair(to, frm):
                continue
            bad = []
            for v in (high(to) + 1, high(frm)):
                if low(frm) <= v <= high(frm) and not low(to) <= v <= high(to) and v not in bad:
                    bad.append(v)
                    break
            for v in (low(to) - 1, low(frm)):
                if low(frm) <= v <= high(frm) and not low(to) <= v <= high(to) and v not in bad:
                    bad.append(v)
                    break
            for j, v in enumerate(bad):
                name = 'nf_%s_%d' % (tag, j)
                values[name] = (head + 'export I64 run() { %s a = %s; return cv(a) as I64; }' % (frm, lit(frm, v)))
                forms[name] = ['+' + helper]
    return values, forms


def shift_programs():
    values, forms = {}, {}
    for t, (bits, signed) in TYPES.items():
        s = t.lower()
        ks = sorted({0, 1, bits // 2, bits - 1})
        fns = ''.join('%s s%d(%s x) { return x >> %d; }\n' % (t, k, t, k) for k in ks)
        xs = edges(t) + ([] if not signed else [low(t) // 3, -3])
        decl = ''.join('%s a%d = %s; ' % (t, i, lit(t, v)) for i, v in enumerate(xs))
        calls = ['s%d(a%d) as I64' % (k, i) for k in ks for i in range(len(xs))]
        # an unsigned result may not fit I64, so the hash takes it through a mask first
        if not signed and bits == 64:
            calls = ['(s%d(a%d) >> 1) as I64' % (k, i) for k in ks for i in range(len(xs))]
        values['shr_' + s] = fns + 'export I64 run() { I64 t = 0; ' + decl + fold(calls) + 'return t; }'
        forms['shr_' + s] = ['-cint_shr_' + s + '(', '+cint_rt_asr(' if signed else '-cint_rt_asr(']
        # a count of the width, and over it: E_SHIFT through the helper
        for k in (bits, 100):
            name = 'shr_%s_over%d' % (s, k)
            values[name] = ('%s sh(%s x) { return x >> %d; }\n' % (t, t, k) +
                            'export I64 run() { %s a = %s; return sh(a) as I64; }' % (t, lit(t, high(t))))
            forms[name] = ['+cint_shr_' + s + '(']
        # a negative count (a signed constant) is E_SHIFT through the helper too
        name = 'shr_%s_negative' % s
        values[name] = ('%s sh(%s x) { return x >> -1; }\n' % (t, t) +
                        'export I64 run() { %s a = 5; return sh(a) as I64; }' % t)
        forms[name] = ['+cint_shr_' + s + '(']
        # a count that is a parameter keeps the helper, in range and out of range
        for k in (1, bits):
            name = 'shr_%s_var%d' % (s, k)
            values[name] = ('%s sh(%s x, I64 k) { return x >> k; }\n' % (t, t) +
                            'export I64 run() { %s a = %s; return sh(a, %d) as I64; }' % (t, lit(t, high(t) // 2 + 1), k))
            forms[name] = ['+cint_shr_' + s + '(']
        # a left shift by a constant keeps its helper
        name = 'shl_' + s
        values[name] = ('%s sh(%s x) { return x << 1; }\n' % (t, t) +
                        'export I64 run() { %s a = 3; return sh(a) as I64; }' % t)
        forms[name] = ['+cint_shl_' + s + '(']
    return values, forms


VALUES, FORMS = conversion_programs()
_v, _f = shift_programs()
VALUES.update(_v)
FORMS.update(_f)

# A function whose only checks are plain has no fault exit of its own (a C compiler warns on an
# unused label: the build of these programs fails if one is written), and a plain node leaves the
# fault record and fuel of its neighbors as they were.
VALUES['no_label'] = ('export I64 widen(I32 x) { return (x as I64) >> 2; }\n'
                      'export I64 run() { return widen(-1000) * 10 + widen(1000); }')
FORMS['no_label'] = ['-cint_as_i64_from_i32(', '-cint_shr_i64(']
VALUES['fault_after'] = ('export I64 run() { I64 t = 9223372036854775000; I32 i = 0; '
                         'while (i < 2000) { t = t + ((i as I64) >> 1); i = i + 1; } return t; }')
FORMS['fault_after'] = ['-cint_as_i64_from_i32(', '-cint_shr_i64(', '+cint_add_i64']
# a counted loop's copy takes the plain forms too
VALUES['counted'] = ('export I64 run() { I64 t = 0; I32 k = 0; I32 n = 300; '
                     'while (k < n) { t += (k as I64) >> 2; k = k + 1; } return t; }')
FORMS['counted'] = ['-cint_as_i64_from_i32(', '-cint_shr_i64(']


def forms(out, names, table=None):
    """Each line of table (FORMS) that the emitted C of out/<case> breaks, for the cases names."""
    return proven_checks_forms(out, names, FORMS if table is None else table)


def proven_checks_forms(out, names, table):
    import proven_checks
    return proven_checks.forms(out, names, table)


if __name__ == '__main__':
    status = runner.main(VALUES, {}, {}, library=LIBRARY, label='plain conversions')
    out = runner.pathlib.Path(sys.argv[sys.argv.index('--out') + 1])
    names = [sys.argv[j + 1] for j, arg in enumerate(sys.argv) if arg == '--only'] or list(FORMS)
    broken = forms(out, names)
    for line in broken:
        print('form: ' + line)
    print('plain conversions forms: %d of %d programs as written' % (len(names) - len({b.split(':')[0] for b in broken}),
                                                                      len(names)))
    raise SystemExit(status or (1 if broken else 0))
