"""Compares declaration extent values, faults, and first-error ordering."""
import constant_extents as ext

CASES = {'field_named': 'const I64 N = 2;\n'
                'struct R { I64[N] a; }\n'
                'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'field_forward': 'struct R { I64[N] a; }\n'
                  'const I64 N = 2;\n'
                  'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'field_dependency': 'const I64 N = M + 1;\n'
                     'const I64 M = 1;\n'
                     'struct R { I64[N] a; }\n'
                     'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'field_expression': 'struct R { I64[(1 + 3) / 2] a; }\n'
                     'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'field_short_circuit': 'struct R { I64[true ? 2 : 1 / 0] a; }\n'
                        'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'field_hex': 'struct R { I64[0x2] a; }\nexport I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'field_zero': 'struct R { I64[1 - 1] a; }\nexport I64 run() { return 0; }\n',
 'field_wrong_width': 'struct R { I64[2 as U8] a; }\nexport I64 run() { return 0; }\n',
 'field_nonconstant': 'I64 N = 2;\nstruct R { I64[N] a; }\nexport I64 run() { return 0; }\n',
 'field_fault': 'struct R { I64[1 / 0] a; }\nexport I64 run() { return 0; }\n',
 'field_unknown_before_fault': 'struct R { Missing[1 / 0] a; }\nexport I64 run() { return 0; }\n',
 'field_duplicate_before_fault': 'struct R { I64 a; I64[1 / 0] a; }\nexport I64 run() { return 0; }\n',
 'field_fault_before_later_duplicate': 'struct R { I64[1 / 0] a; I64 a; }\n'
                                       'export I64 run() { return 0; }\n',
 'field_fault_before_cycle': 'struct Cycle { Cycle c; }\n'
                             'struct R { I64[1 / 0] a; }\n'
                             'export I64 run() { return 0; }\n',
 'field_cycle_constant': 'const I64 N = M;\n'
                         'const I64 M = N;\n'
                         'struct R { I64[N] a; }\n'
                         'export I64 run() { return 0; }\n',
 'view_named': 'const I64 N = 2;\n'
               'I64 f(in I64[N] a) { return a[1]; }\n'
               'export I64 run() { I64[2] a; a[1] = 7; return f(a); }\n',
 'view_expression': 'I64 f(in I64[0x1 + 1] a) { return a[1]; }\n'
                    'export I64 run() { I64[2] a; a[1] = 7; return f(a); }\n',
 'view_forward': 'I64 f(in I64[N] a) { return a[1]; }\n'
                 'const I64 N = 2;\n'
                 'export I64 run() { I64[2] a; a[1] = 7; return f(a); }\n',
 'view_wrong_width': 'I64 f(in I64[2 as U8] a) { return 0; }\n'
                     'export I64 run() { I64[2] a; a[1] = 7; return f(a); }\n',
 'view_fault_before_result': 'Missing f(in I64[1 / 0] a) { return 0; }\n'
                             'export I64 run() { return 0; }\n',
 'view_duplicate_size_before_fault': 'I64 f[n,n](in I64[1 / 0] a) { return 0; }\n'
                                     'export I64 run() { return 0; }\n',
 'view_unbound_size_before_result': 'Missing f[n](in I64[1 + 1] a) { return 0; }\n'
                                    'export I64 run() { return 0; }\n',
 'view_size_expression': 'I64 f[n](in I64[n] a, in I64[n + 1] b) { return 0; }\n'
                         'export I64 run() { return 0; }\n',
 'field_imported_constant': 'import ext;\n'
                            'struct R { I64[ext.N] a; }\n'
                            'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'view_imported_constant': 'import ext;\n'
                           'I64 f(in I64[ext.N] a) { return a[1]; }\n'
                           'export I64 run() { I64[2] a; a[1] = 7; return f(a); }\n',
 'view_record': 'struct R { I64 v; }\n'
                'const I64 N = 2;\n'
                'I64 take(in R[N] a) { return a[1].v; }\n'
                'export I64 run() { R[2] a; a[1].v = 9; return take(a); }\n',
 'view_imported_record': 'import ext;\n'
                         'const I64 N = 2;\n'
                         'I64 take(in ext.R[N] a) { return a[1].v; }\n'
                         'export I64 run() { ext.R[2] a; a[1].v = 9; return take(a); }\n',
 'field_early_call': 'struct R { I64[f()] a; }\nI64 f() { return 2; }\nexport I64 run() { return 0; }\n',
 'field_early_call_width': 'struct R { I64[f()] a; }\n'
                           'U8 f() { return 2; }\n'
                           'export I64 run() { return 0; }\n',
 'field_early_call_param': 'struct R { I64[f(2)] a; }\n'
                           'I64 f(U8 n) { return n as I64; }\n'
                           'export I64 run() { return 0; }\n',
 'field_early_call_unknown': 'struct R { I64[f(2)] a; }\n'
                             'I64 f(Missing n) { return 0; }\n'
                             'export I64 run() { return 0; }\n',
 'field_modvar_width': 'U8 N = 2;\nstruct R { I64[N] a; }\nexport I64 run() { return 0; }\n',
 'field_modvar_const': 'I64 V = 2;\n'
                       'const I64 N = V;\n'
                       'struct R { I64[N] a; }\n'
                       'export I64 run() { return 0; }\n',
 'view_zero': 'const I64 N = 0;\n'
              'I64 take(in I64[N] a) { return 9; }\n'
              'export I64 run() { I64[0] a; return take(a); }\n'}
CASES['field_call_arity_before_type'] = 'struct R { I64[f()] a; }\nI64 f(Missing n) { return 0; }\nexport I64 run() { return 0; }\n'
CASES['view_encoding_limit'] = 'I64 take(in U8[65533] a) { return 9; }\nexport I64 run() { return 0; }\n'
CASES['view_encoding_boundary'] = 'I64 take(in U8[65534] a) { return 9; }\nexport I64 run() { return 0; }\n'
CASES['record_field_named'] = 'struct Leaf { I64 value; }\nstruct Box { Leaf[N] items; }\nconst I64 N = 1 + 1;\nexport I64 run() { Box first; first.items[1].value = 17; Box second = first; first.items[1].value = 99; return second.items[1].value; }\n'
CASES['imported_record_field_named'] = 'import ext;\nstruct Box { ext.R[N] items; }\nconst I64 N = 1 + 1;\nexport I64 run() { Box first; first.items[1].v = 23; Box second = first; return second.items[1].v; }\n'


if __name__ == '__main__':
    ext.CASES = CASES
    ext.BOUNDARY = {'field_zero', 'view_encoding_boundary'}
    raise SystemExit(ext.main())
