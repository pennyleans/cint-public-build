"""Compares constructed record field reads and their diagnostics with cint_ref."""
import named_constructors as constructors


VALUES = {
    'local': 'struct R { I64 value; }\nexport I64 run() { return R(7).value; }\n',
    'imported': 'export I64 run() { return records.Leaf(7).value; }\n',
    'named': 'export I64 run() { return records.R(second = 2, first = 7).first; }\n',
    'parenthesized': 'export I64 run() { return (records.Leaf(7)).value; }\n',
    'nested': 'export I64 run() { return records.Outer(records.Leaf(7), 0).item.value; }\n',
    'bool': 'export Bool run() { return records.Flag(true, 7).active; }\n',
    'record_copy': 'export I64 run() { records.Leaf r = records.Outer(records.Leaf(7), 0).item; return r.value; }\n',
    'record_argument': 'I64 take(records.Leaf r) { return r.value; }\n'
                       'export I64 run() { return take(records.Outer(records.Leaf(7), 0).item); }\n',
    'constructor_argument': 'export I64 run() { records.Outer r = records.Outer(item = records.Outer(records.Leaf(7), 0).item, tail = 0); return r.item.value; }\n',
    'written_order': 'I64 step(inout I64[1] a) { a[0] += 1; return a[0]; }\n'
                     'export I64 run() { I64[1] a; return records.R(step(a), step(a)).second * 10 + a[0]; }\n',
    'scalar_snapshot': 'I64 take(I64 first, I64 second) { return first; }\n'
                        'I64 mutate(inout I64[1] a) { a[0] = 9; return 0; }\n'
                        'export I64 run() { I64[1] a; a[0] = 1; return take(records.Leaf(a[0]).value, mutate(a)); }\n',
    'record_snapshot': 'I64 take(records.Leaf first, I64 second) { return first.value; }\n'
                        'I64 mutate(inout I64[1] a) { a[0] = 9; return 0; }\n'
                        'export I64 run() { I64[1] a; a[0] = 1; return take(records.Outer(records.Leaf(a[0]), 0).item, mutate(a)); }\n',
    'early_bounds': 'I64 step(inout I64[1] a) { a[0] += 1; return a[0]; }\n'
                    'export I64 run() { I64[1] a; I64 index = 1; return records.R(a[index], step(a)).first; }\n',
    'unread_field_fault': 'I64 step(inout I64[1] a) { a[0] += 1; return a[0]; }\n'
                          'export I64 run() { I64[1] a; return records.R(7, I64.max + step(a)).first; }\n',
    'short_circuit': 'I64 step(inout I64[1] a) { a[0] += 1; return a[0]; }\n'
                      'export I64 run() { I64[1] a; Bool b = false && records.Flag(true, I64.max + step(a)).active; if (b) { return 1; } return a[0]; }\n',
    'nested_local': 'struct Leaf { I64 value; }\nstruct Outer { Leaf item; }\n'
                     'export I64 run() { return Outer(Leaf(7)).item.value; }\n',
    'returned_record_control': 'records.Leaf make() { return records.Leaf(7); }\n'
                               'export I64 run() { return make().value; }\n',
}

DIAGNOSTICS = {
    'local_missing_field': 'struct R { I64 value; }\nexport I64 run() { return R(7).absent; }\n',
    'imported_missing_field': 'export I64 run() { return records.Leaf(7).absent; }\n',
    'nested_missing_field': 'export I64 run() { return records.Outer(records.Leaf(7), 0).item.absent; }\n',
    'constructor_before_field': 'export I64 run() { return records.Leaf(true).absent; }\n',
    'private_before_field': 'export I64 run() { return records.Hidden(7).absent; }\n',
    'scalar_base': 'export I64 run() { return (7).value; }\n',
    'temporary_write': 'export I64 run() { records.Leaf(7).value = 9; return 0; }\n',
    'temporary_nested_write': 'export I64 run() { records.Outer(records.Leaf(7), 0).item.value = 9; return 0; }\n',
    'temporary_inout': 'void change(inout records.Leaf r) { r.value = 9; }\n'
                       'export I64 run() { change(records.Outer(records.Leaf(7), 0).item); return 0; }\n',
}


if __name__ == '__main__':
    raise SystemExit(constructors.main(VALUES, DIAGNOSTICS, {}, label='constructed fields'))
