"""Exercises contextual identifiers and case-sensitive built-in type names."""

CASES = {'actual_round_type': {'main.ci': 'export I64 run() { Round value; return 0; }\n'},
 'at_field_type': {'main.ci': 'struct at { I64 value; }\n'
                              'struct R { at item; }\n'
                              'export I64 run() { R r = R(at(7)); return r.item.value; }\n'},
 'at_global': {'main.ci': 'const I64 at = 7;\nexport I64 run() { return at; }\n'},
 'at_import': {'at.ci': 'export const I64 value = 7;\n',
               'main.ci': 'import at;\nexport I64 run() { return at.value; }\n'},
 'at_import_type': {'main.ci': 'import records;\n'
                               'export I64 run() { records.at r; r.value = 7; return r.value; }\n',
                    'records.ci': 'export struct at { I64 value; }\n'},
 'at_local': {'main.ci': 'export I64 run() { I64 at = 7; return at; }\n'},
 'at_parameter': {'main.ci': 'I64 f(I64 at) { return at; }\nexport I64 run() { return f(7); }\n'},
 'at_record_parameter': {'main.ci': 'struct at { I64 value; }\n'
                                    'I64 take(at r) { return r.value; }\n'
                                    'export I64 run() { at r = at(7); return take(r); }\n'},
 'at_size': {'main.ci': 'I64 f[at](in I64[at] values) { return values[0]; }\n'
                        'export I64 run() { I64[1] values; values[0] = 7; return f(values); }\n'},
 'at_type': {'main.ci': 'struct at { I64 value; }\nexport I64 run() { at r = at(7); return r.value; }\n'},
 'at_unknown_type': {'main.ci': 'export I64 run() { at value; return 0; }\n'},
 'contextual_at': {'main.ci': 'I64 divide(I64 n) { return 1 / n; }\n'
                              'test "fault" expect_fault E_DIV_ZERO at 1 { _ = divide(0); }\n'
                              'export I64 run() { return 7; }\n'},
 'contextual_at_invalid': {'main.ci': 'test "fault" expect_fault E_DIV_ZERO at false {}\n'
                                      'export I64 run() { return 0; }\n'},
 'conversion': {'main.ci': 'export I64 run() { return 7 as I64 round floor; }\n'},
 'duplicate': {'main.ci': 'export I64 run() { I64 round = 1; I64 round = 7; return round; }\n'},
 'field': {'main.ci': 'struct R { I64 round; }\n'
                      'export I64 run() { R r = R(round = 7); return r.round; }\n'},
 'function': {'main.ci': 'I64 round(I64 value) { return value; }\nexport I64 run() { return round(7); }\n'},
 'global_const': {'main.ci': 'const I64 round = 7;\nexport I64 run() { return round; }\n'},
 'global_var': {'main.ci': 'I64 round = 7;\nexport I64 run() { return round; }\n'},
 'import': {'main.ci': 'import round;\nexport I64 run() { return round.value; }\n',
            'round.ci': 'export const I64 value = 7;\n'},
 'local': {'main.ci': 'export I64 run() { I64 round = 7; return round; }\n'},
 'lowercase_struct': {'main.ci': 'struct round { I64 value; }\n'
                                 'export I64 run() { round r = round(7); return r.value; }\n'},
 'parameter': {'main.ci': 'I64 f(I64 round) { return round; }\nexport I64 run() { return f(7); }\n'},
 'round_array_type': {'main.ci': 'struct round { I64 value; }\n'
                                 'I64 f(in round[_] a) { return a[0].value; }\n'
                                 'export I64 run() { round[1] a; a[0].value = 7; return f(a); }\n'},
 'round_import_type': {'main.ci': 'import records;\n'
                                  'export I64 run() { records.round r; r.value = 7; return r.value; '
                                  '}\n',
                       'records.ci': 'export struct round { I64 value; }\n'},
 'round_record_parameter': {'main.ci': 'struct round { I64 value; }\n'
                                       'I64 take(round r) { return r.value; }\n'
                                       'export I64 run() { round r = round(7); return take(r); }\n'},
 'round_unknown_type': {'main.ci': 'export I64 run() { round value; return 0; }\n'},
 'size': {'main.ci': 'I64 f[round](in I64[round] values) { return values[0]; }\n'
                     'export I64 run() { I64[1] values; values[0] = 7; return f(values); }\n'},
 'uppercase_global': {'main.ci': 'const I64 Round = 7;\nexport I64 run() { return Round; }\n'},
 'uppercase_import': {'Round.ci': 'export const I64 value = 7;\n',
                      'main.ci': 'import Round;\nexport I64 run() { return 0; }\n'},
 'uppercase_local': {'main.ci': 'export I64 run() { I64 Round = 7; return Round; }\n'},
 'uppercase_parameter': {'main.ci': 'I64 f(I64 Round) { return Round; }\n'
                                    'export I64 run() { return f(7); }\n'},
 'uppercase_size': {'main.ci': 'I64 f[Round](in I64[Round] values) { return values[0]; }\n'
                               'export I64 run() { return 0; }\n'},
 'uppercase_struct': {'main.ci': 'struct Round { I64 value; }\nexport I64 run() { return 0; }\n'}}
