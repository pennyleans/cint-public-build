"""Compares array literal arguments of view parameters with cint_ref."""
import named_constructors as runner

TOTAL = 'I64 total[n](in I64[n] xs, I64 base) { I64 t = base; for i in 0..n { t += xs[i]; } return t; }\n'
DOT = 'I64 inner[n](in I64[n] a, in I64[n] b) { I64 t = 0; for i in 0..n { t += a[i] * b[i]; } return t; }\n'
MARK = 'I64 seen = 0;\nI64 mark(I64 v) { seen = seen * 10 + v; return v; }\n'
VALUES = {
    'sized': TOTAL + 'export I64 run() { return total([200, 300, 7], 825); }',
    'any_extent': 'I64 head(in I64[_] xs) { return xs[0]; }\nexport I64 run() { return head([42]); }',
    'fixed_extent': 'I64 sum3(in I64[3] xs) { return xs[0] + xs[1] + xs[2]; }\n'
                    'export I64 run() { return sum3([1, 20, 300]); }',
    'rank2': 'I64 corner(in I64[_, _] m) { return m[1, 0] * 10 + m[0, 1]; }\n'
             'export I64 run() { return corner([[1, 2], [3, 4]]); }',
    'rank2_sized': 'I64 trace[r, c](in I64[r, c] m) { I64 t = 0; for i in 0..r { t += m[i, i]; } return t * 10 + c; }\n'
                   'export I64 run() { return trace([[1, 2, 3], [4, 5, 6]]); }',
    'u8': 'I64 pick(in U8[_] bs, I64 k) { return bs[k] as I64; }\nexport I64 run() { return pick([1, 200], 1); }',
    'bool': 'I64 tally(in Bool[_] bs) { I64 t = 0; for i in 0..3 { if (bs[i]) { t += 1; } } return t; }\n'
            'export I64 run() { return tally([true, false, true]); }',
    'two_literals': DOT + 'export I64 run() { return inner([1, 2], [3, 4]); }',
    'named_and_literal': DOT + 'export I64 run() { I64[2] a = [5, 6]; return inner(a, [7, 8]); }',
    'items_in_order': TOTAL + MARK + 'export I64 run() { I64 t = total([mark(1), mark(2), mark(3)], mark(4)); '
                      'return t * 10000 + seen; }',
    'item_fault': TOTAL + 'export I64 run() { I64 x = 9223372036854775807; return total([1, x + 1], 0); }',
    'element_fault': 'I64 at(in I64[_] xs, I64 k) { return xs[k]; }\nexport I64 run() { return at([1, 2], 2); }',
    'empty': 'I64 seven(in I64[_] xs) { return 7; }\nexport I64 run() { return seven([]); }',
    'rank3': 'I64 c3(in I64[_, _, _] m) { return m[1, 0, 1] * 100 + m[0, 1, 0]; }\n'
             'export I64 run() { return c3([[[1, 2], [3, 4]], [[5, 6], [7, 8]]]); }',
    'literal_in_item': 'I64 f(in I64[_] xs, in I64[_] ys) { return xs[0] * 10 + ys[1]; }\n'
                       'export I64 run() { I64 a = 4; return f([a, a + 1], [a * 2, f([1], [2, 3])]); }',
    'loop': TOTAL +'export I64 run() { I64 t = 0; for i in 0..3 { t += total([i, i * 2], 0); } return t; }',
    'nested_call': TOTAL + 'export I64 run() { return total([total([1, 2], 0), 3], 0); }',
    'imported': 'export I64 run() { return records.first([5, 6]); }',
    'void_callee': 'I64 seen = 0;\nvoid keep(in I64[_] xs) { seen = xs[1]; }\n'
                   'export I64 run() { keep([3, 9]); return seen; }',
}
DIAGNOSTICS = {
    'inout': 'void bump(inout I64[_] xs) { xs[0] += 1; }\nexport I64 run() { bump([1, 2]); return 0; }',
    'count_mismatch': 'I64 sum3(in I64[3] xs) { return xs[0]; }\nexport I64 run() { return sum3([1, 2]); }',
    'element_mismatch': 'I64 head(in I64[_] xs) { return xs[0]; }\nexport I64 run() { return head([true]); }',
    'element_range': 'I64 pick(in U8[_] bs) { return bs[0] as I64; }\nexport I64 run() { return pick([256]); }',
    'scalar_parameter': 'I64 one(I64 a) { return a; }\nexport I64 run() { return one([1]); }',
    'ragged': 'I64 corner(in I64[_, _] m) { return m[0, 0]; }\nexport I64 run() { return corner([[1, 2], [3]]); }',
    'shape_mismatch': DOT + 'export I64 run() { return inner([1, 2], [3]); }',
    'imported_inout': 'export I64 run() { records.bump([1]); return 0; }',
}

if __name__ == '__main__':
    raise SystemExit(runner.main(VALUES, DIAGNOSTICS, {},
        library='export I64 first(in I64[_] xs) { return xs[0]; }\nexport void bump(inout I64[_] xs) { xs[0] += 1; }\n',
        label='array literal arguments'))
