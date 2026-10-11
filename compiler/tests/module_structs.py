"""Compares module-level structs (decision 2026-10-09 on OQ-260; rt/OPEN.md RT-OQ-51) with
cint_ref: zero-filled and initialized structs of integers and Bool, with nested structs, array
fields of rank 1 to 4 and arrays of structs, positional and named constructors, and wide fields
(box 15 unit 4a), read and written by several functions, passed by place, copied whole, of a type
another module declares, passed to another module's functions of wide signatures (unit 4c), and
faulting; the initializers cint_ref refuses; and the forms cintc refuses with C9102 that
cint_ref runs (an exported struct, an array of structs, more than 65,536 bytes of payload, a
field of no storage, a constructor's array literal inside a function, and, inside a function,
an array literal of structs or a view variable of structs)."""
import argparse
import pathlib

import module_arrays as ma
import named_constructors as runner

DIGITS = 'I64 digits(in I64[_] v) { I64 t = 0; for i in 0..len(v) { t = t * 10 + v[i]; } return t; }\n'
P = 'struct P { I64 x; I64 y; }\n'
VALUES = {
    'zero_filled': P + 'P p;\nexport I64 run() { p.y = 5; return p.x * 10 + p.y; }',
    'initialized': P + 'P p = P(3, 4);\nexport I64 run() { p.x += 40; return p.x * 10 + p.y; }',
    'named': P + 'P p = P(y = 4, x = 3);\nexport I64 run() { return p.x * 10 + p.y; }',
    'mixed_named': P + 'P p = P(3, y = 4);\nexport I64 run() { return p.x * 10 + p.y; }',
    'scalars': 'struct M { U8 a; I8 b; Bool c; U64 d; I16 e; }\n'
               'M m = M(250, -128, true, 18446744073709551615, -2);\n'
               'export I64 run() { m.a += 5; m.b += 1; m.c = !m.c; I64 t = (m.a as I64) * 1000 + (m.b as I64); '
               'if (!m.c) { t = t * 10 + 1; } return t * 100 + (m.e as I64) + (m.d % 7) as I64; }',
    'constant_items': 'const I64 K = 5;\n' + P + 'P p = P(K, K * 2 + 1);\nexport I64 run() { return p.x * 100 + p.y; }',
    'nested': 'struct Q { I64 a; Bool b; }\nstruct S { I64[3] pos; Q q; I64 tick; }\n'
              'S s = S([1, 2, 3], Q(4, true), 7);\n'
              'export I64 run() { s.tick += 10; s.q.b = !s.q.b; I64 t = s.tick + s.pos[2] + s.q.a; '
              'if (s.q.b) { t += 1000; } return t; }',
    'nested_named': 'struct Q { I64 a; Bool b; }\nstruct S { Q q; I64 tick; }\n'
                    'S s = S(tick = 7, q = Q(b = true, a = 4));\nexport I64 run() { return s.tick * 10 + s.q.a; }',
    'array_field': DIGITS + 'struct A { I64[3] xs; I64 n; }\nA a = A([4, 5, 6], 2);\n'
                   'export I64 run() { a.xs[a.n] += 1; return digits(a.xs) * 10 + a.n; }',
    'rank2_field': 'struct St { I64[2, 3] pos; I64 tick; Bool stopped; }\n'
                   'St committed = St([[1, 2, 3], [4, 5, 6]], 7, false);\nSt stage;\n'
                   'export I64 run() { stage = committed; stage.tick = committed.tick + 10; stage.pos[1, 2] = 10; '
                   'stage.stopped = true; committed = stage; return committed.tick + committed.pos[1, 2] + '
                   'committed.pos[0, 1]; }',
    'rank4_field': 'struct F { Bool[2, 2] on; U8[2, 3, 2] code; I16[1, 2, 2, 3] four; }\nF f;\n'
                   'export I64 run() { f.on[1, 0] = true; f.code[1, 2, 1] = 200 as U8; '
                   'f.four[0, 1, 1, 2] = -3 as I16; I64 t = (f.code[1, 2, 1] as I64) * 10 + f.four[0, 1, 1, 2] as I64; '
                   'if (f.on[1, 0]) { t += 100000; } return t; }',
    'array_of_struct_field': 'struct Q { I64 a; Bool b; }\nstruct S { Q[2] q; I64 n; }\n'
                             'S s = S([Q(1, false), Q(2, true)], 3);\n'
                             'export I64 run() { s.q[0].a += s.n; I64 t = s.q[0].a * 10 + s.q[1].a; '
                             'if (s.q[1].b) { t += 100; } return t; }',
    'across_functions': P + 'P acc;\nvoid add(I64 v) { acc.x += v; acc.y += 1; }\n'
                        'export I64 run() { add(7); add(5); return acc.x * 10 + acc.y; }',
    'with_scalar_and_array': 'I64 n = 3;\nI64[2] xs = [10, 20];\n' + P + 'P p = P(1, 2);\n'
                             'export I64 run() { n += xs[1]; p.x = n; xs[0] = p.y; return p.x * 100 + xs[0]; }',
    'inout_argument': P + 'P p = P(1, 2);\nvoid bump(inout P q) { q.x += 7; }\n'
                      'export I64 run() { bump(p); return p.x * 10 + p.y; }',
    'in_argument': P + 'P p = P(3, 4);\nI64 norm(in P q) { return q.x * q.x + q.y * q.y; }\n'
                   'export I64 run() { return norm(p); }',
    'field_view_argument': DIGITS + 'struct A { I64[3] xs; }\nA a = A([1, 2, 3]);\n'
                           'void bump(inout I64[_] v) { v[0] += 7; }\nexport I64 run() { bump(a.xs); return digits(a.xs); }',
    'copy_in_and_out': P + 'P p = P(1, 2);\n'
                       'export I64 run() { P q = p; q.x = 9; p = q; return p.x * 100 + q.y * 10 + p.y; }',
    'assigned_from_call': P + 'P p;\nP make(I64 v) { return P(v, v + 1); }\n'
                          'export I64 run() { p = make(4); return p.x * 10 + p.y; }',
    'assigned_constructor': P + 'P p = P(1, 2);\nexport I64 run() { p = P(p.y, p.x); return p.x * 10 + p.y; }',
    'imported_type': 'records.Pt pt = records.Pt(1, 2);\nrecords.Pt other;\n'
                     'export I64 run() { pt.x += 2; other = pt; return records.norm(other) * 10 + pt.y; }',
    'imported_function_inout': 'records.Pt pt = records.Pt(y = 5, x = 1);\n'
                               'export I64 run() { records.grow(pt); return pt.x * 10 + pt.y; }',
    'unnamed': P + 'P p = P(1, 2);\nI64 other() { return 1; }\nexport I64 run() { return other(); }',
    'bounds_fault': 'struct A { I64[2] xs; }\nA a;\nexport I64 run() { I64 k = 2; return a.xs[k]; }',
    'overflow_fault': P + 'P p = P(9223372036854775807, 0);\nexport I64 run() { p.x += 1; return 0; }',
    'payload_bytes': 'struct A { I64[512] a; I64[512] b; }\n'
                     'struct B { A a1; A a2; A a3; A a4; A a5; A a6; A a7; A a8; }\nB big;\n'
                     'export I64 run() { big.a8.b[511] = 3; big.a1.a[0] -= 1; return big.a8.b[511] + big.a1.a[0]; }',
    # Wide fields (box 15 unit 4a): a zero-filled array of wide elements is braced to its first
    # limb, which GCC's -Wmissing-braces requires.
    'wide_array_field': 'struct S { I128[4] g; I64 n; }\nS keep;\n'
                        'export I64 run() { keep.n = 3; keep.g[3] = 5 as I128; '
                        'return keep.n * 10 + ((keep.g[3] + keep.g[0]) as I64); }',
    'wide_fields': 'struct T { I64 a; I512[2] w; I128 s; }\nstruct S { T[2] t; I1024[3] g; I64 n; }\n'
                   'I256[2, 3] grid;\nI128[3] ga = [1, -2, 3];\nS keep;\n'
                   'export I64 run() { keep.g[2] = I1024.max; keep.t[1].w[1] = -5 as I512; grid[1, 2] = 9 as I256; '
                   'return ((keep.g[2] >> 1000) as I64) * 1000 + (keep.t[1].w[1] as I64) * 100 + '
                   '((grid[1, 2] + grid[0, 0]) as I64) * 10 + ((ga[1] + keep.t[0].s) as I64); }',
    'wide_initialized': 'struct W { I128[2] g; I256 s; I64 n; }\nW w = W([1, -2], 3, 4);\n'
                        'export I64 run() { w.g[1] += 10 as I128; '
                        'return ((w.g[0] + w.g[1]) as I64) * 100 + (w.s as I64) * 10 + w.n; }',
    # Box 15 unit 4c: another module's functions of wide signatures.
    'imported_wide_signatures': 'struct W { I512[3] v; }\nW w;\n'
                                'export I64 run() { records.fill_sq(w.v); I256 t = records.twice(I256.max >> 2); '
                                'return ((t >> 250) as I64) * 100 + (w.v[2] as I64) * 10 + '
                                '(records.sum3([1, 2, 3], 4) as I64); }',
    'imported_wide_fault': 'export I64 run() { return (records.twice(I256.max) >> 255) as I64; }',
}
DIAGNOSTICS = {
    'call_item': P + 'I64 one() { return 1; }\nP p = P(one(), 2);\nexport I64 run() { return p.x; }',
    'imported_call_item': P + 'P p = P(records.one(), 2);\nexport I64 run() { return p.x; }',
    'nested_call_item': 'struct Q { I64 a; }\nstruct S { Q q; I64 b; }\nI64 one() { return 1; }\n'
                        'S s = S(Q(one()), 2);\nexport I64 run() { return s.b; }',
    'variable_item': P + 'I64 v = 1;\nP p = P(v, 2);\nexport I64 run() { return p.x; }',
    'field_type': P + 'P p = P(true, 2);\nexport I64 run() { return p.x; }',
    'missing_field': P + 'P p = P(1);\nexport I64 run() { return p.x; }',
    'extra_field': P + 'P p = P(1, 2, 3);\nexport I64 run() { return p.x; }',
    'element_range': 'struct M { U8 a; I64 b; }\nM m = M(256, 2);\nexport I64 run() { return m.b; }',
    'item_fault': P + 'P p = P(9223372036854775807 + 1, 2);\nexport I64 run() { return p.y; }',
    'scalar_initializer': P + 'P p = 5;\nexport I64 run() { return p.x; }',
    'array_count': 'struct A { I64[3] a; I64 b; }\nA a = A([1, 2], 2);\nexport I64 run() { return a.b; }',
    'row_count': 'struct A { I64[2, 2] a; I64 b; }\nA a = A([[1, 2], [3]], 2);\nexport I64 run() { return a.b; }',
    'flat_items': 'struct A { I64[2, 2] a; I64 b; }\nA a = A([1, 2, 3, 4], 2);\nexport I64 run() { return a.b; }',
    'array_struct_item': 'struct Q { I64 a; }\nstruct S { Q[2] q; }\nS s = S([Q(1), 2]);\n'
                         'export I64 run() { return s.q[0].a; }',
    'wrong_struct': P + 'struct T { I64 x; I64 y; }\nP p = T(1, 2);\nexport I64 run() { return p.x; }',
    'duplicate_named': P + 'P p = P(x = 1, x = 2);\nexport I64 run() { return p.x; }',
    'unknown_named': P + 'P p = P(x = 1, z = 2);\nexport I64 run() { return p.x; }',
    'assign_scalar': P + 'P p;\nexport I64 run() { p = 3; return p.x; }',
    'local_named_alike': P + 'P p;\nexport I64 run() { I64 p = 1; return p; }',
    'parameter_named_alike': P + 'P p;\nI64 f(I64 p) { return p + 1; }\nexport I64 run() { return f(4); }',
}
# cint_ref runs these; cintc refuses them with C9102 at the token it names: the type of an
# exported struct, an array of structs, the declaration's name for more than 65,536 bytes of
# payload or a field of no storage, a constructor's array literal inside a function, and an
# array literal of structs or a view variable of structs inside one.
BOUNDARIES = {
    'exported': ('export struct S { I64 a; }\nexport S s;\nexport I64 run() { return s.a; }', 'S s;'),
    'array_of_structs': ('struct S { I64 a; }\nS[2] s;\nexport I64 run() { return s[1].a; }', 'S[2] s'),
    'payload_bytes': ('struct A { I64[512] a; I64[512] b; }\n'
                      'struct B { A a1; A a2; A a3; A a4; A a5; A a6; A a7; A a8; A a9; }\nB big;\n'
                      'export I64 run() { return big.a9.b[1]; }', 'big;'),
    'zero_extent': ('struct S { I64[0] z; I64 a; }\nS zs;\nexport I64 run() { return zs.a; }', 'zs;'),
    'nested_zero_extent': ('struct Z { I64[0] z; }\nstruct S { Z z; I64 a; }\nS zs;\n'
                           'export I64 run() { return zs.a; }', 'zs;'),
    'function_array_argument': ('struct S { I64[2] a; }\nexport I64 run() { S s = S([1, 2]); return s.a[1]; }',
                                '[1, 2]'),
    'local_array_literal': ('struct S { I64 a; I64 b; }\n'
                            'export I64 run() { S[2] s = [S(1, 2), S(3, 4)]; return s[1].a; }', '[S(1, 2)'),
    'array_literal_argument': ('struct S { I64 a; I64 b; }\nI64 first(in S[_] v) { return v[0].b; }\n'
                               'export I64 run() { return first([S(1, 2), S(3, 4)]); }', '[S(1, 2)'),
    'view_variable': ('struct S { I64 a; }\n'
                      'export I64 run() { S[2] s; s[1].a = 5; in S[_] v = s; return v[1].a; }', 'v = s'),
}
LIBRARY = ('export struct Pt { I64 x; I64 y; }\nexport I64 one() { return 1; }\n'
           'export I64 norm(in Pt p) { return p.x * p.x + p.y * p.y; }\nexport void grow(inout Pt p) { p.x += 1; }\n'
           'export I256 twice(I256 a) { return a * 2; }\n'
           'export void fill_sq(inout I512[_] v) { for i in 0..len(v) { v[i] = (i as I512) * (i as I512) - 1; } }\n'
           'export I1024 sum3(in I1024[_] v, I64 k) { return v[0] + v[1] + v[2] + (k as I1024); }\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--exe', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path)
    parser.add_argument('--only', action='append')
    known, _ = parser.parse_known_args()
    status = runner.main(VALUES, DIAGNOSTICS, {}, library=LIBRARY, label='module-level structs')
    if not known.only:   # the boundary cases run with the whole set
        status = 1 if ma.boundaries(known.exe, known.out / 'boundaries', BOUNDARIES, LIBRARY) else status
    raise SystemExit(status)
