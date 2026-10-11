"""Compares module-level arrays (SPEC-04 LS-113, OQ-233; rt/OPEN.md RT-OQ-49) with cint_ref:
zero-filled and initialized arrays of integers and Bool, of rank 1 to 4, read and written by
several functions, passed as views, copied, reduced and faulting; the initializers cint_ref
refuses; and the forms cintc refuses with C9102 that cint_ref runs (more than 65,536 bytes of
elements, an extent that is not a literal, another module's array). Module-level structs are
compiler/tests/module_structs.py's."""
import argparse
import json
import pathlib
import subprocess
import sys

import import_views as iv
import named_constructors as runner

ROOT = pathlib.Path(__file__).resolve().parents[2]

DIGITS = 'I64 digits(in I64[_] v) { I64 t = 0; for i in 0..len(v) { t = t * 10 + v[i]; } return t; }\n'
CORNER = 'I64 corner(in I64[_, _] v) { return v[extent(v, 0) - 1, extent(v, 1) - 1]; }\n'
VALUES = {
    'zero_filled': 'I64[3] cents;\nexport I64 run() { cents[1] = 5; return cents[0] * 100 + cents[1] * 10 + cents[2]; }',
    'initialized': 'I64[3] cents = [1, 2, 3];\n'
                   'export I64 run() { cents[2] += 40; return cents[0] * 100 + cents[1] * 10 + cents[2]; }',
    'u8': 'U8[4] bs = [1, 2, 250, 4];\nexport I64 run() { bs[3] = bs[2] + 5; return bs[3] as I64; }',
    'i8': 'I8[2] xs = [-128, 127];\nexport I64 run() { return (xs[0] as I64) * 1000 + xs[1] as I64; }',
    'bool': 'Bool[3] flags = [true, false, true];\n'
            'export I64 run() { I64 t = 0; flags[1] = true; for i in 0..3 { if (flags[i]) { t += 1; } } return t; }',
    'constant_items': 'const I64 K = 5;\nI64[2] xs = [K, K * 2 + 1];\nexport I64 run() { return xs[0] * 100 + xs[1]; }',
    'across_functions': 'I64[4] acc;\nvoid add(I64 i, I64 v) { acc[i] += v; }\n'
                        'export I64 run() { add(1, 7); add(1, 5); add(3, 2); return acc[1] * 10 + acc[3]; }',
    'with_scalar': 'I64 n = 3;\nI64[2] xs = [10, 20];\nexport I64 run() { n += xs[1]; xs[0] = n; return n * 100 + xs[0]; }',
    'view_argument': DIGITS + 'I64[3] xs = [4, 5, 6];\nexport I64 run() { return digits(xs); }',
    'inout_argument': DIGITS + 'I64[2] xs = [1, 2];\nvoid bump(inout I64[_] v) { v[0] += 7; }\n'
                      'export I64 run() { bump(xs); return digits(xs); }',
    'copy_in_and_out': DIGITS + 'I64[2] xs = [1, 2];\n'
                       'export I64 run() { I64[2] y = xs; y[0] = 9; xs = y; return digits(xs) * 100 + digits(y); }',
    'length': 'U8[5] bs;\nexport I64 run() { return len(bs); }',
    'reduction': 'I64[3] xs = [1, 2, 30];\nexport I64 run() { return sum(xs); }',
    'imported_function': 'I64[2] xs = [5, 6];\nexport I64 run() { return records.first(xs); }',
    'bounds_fault': 'I64[2] xs;\nexport I64 run() { I64 k = 2; return xs[k]; }',
    'overflow_fault': 'I64[1] xs = [9223372036854775807];\nexport I64 run() { xs[0] += 1; return 0; }',
    # Rank 2 to 4 (box 13 MLA (c)): each function that names the array views its elements.
    'rank2_zero_filled': 'I64[2, 3] m;\nexport I64 run() { m[1, 2] = 5; m[0, 1] = 3; return m[0, 1] * 10 + m[1, 2]; }',
    'rank2_initialized': 'I32[2, 2] m = [[1, 2], [3, 4]];\nexport I64 run() { m[1, 0] += 10; '
                         'return (m[0, 0] * 1000 + m[0, 1] * 100 + m[1, 0] * 10 + m[1, 1]) as I64; }',
    'rank3': 'U8[2, 2, 2] c = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];\nexport I64 run() { c[1, 0, 1] = 60; I64 t = 0; '
             'for i in 0..2 { for j in 0..2 { for k in 0..2 { t = t * 10 + c[i, j, k] as I64 % 10; } } } return t; }',
    'rank4': 'I16[2, 1, 2, 3] q;\nexport I64 run() { q[1, 0, 1, 2] = -7; q[0, 0, 0, 0] = 3; '
             'return (q[1, 0, 1, 2] as I64) * 100 + q[0, 0, 0, 0] as I64 * 10 + extent(q, 3); }',
    'rank2_bool': 'Bool[2, 2] f = [[true, false], [false, true]];\nexport I64 run() { f[0, 1] = true; I64 t = 0; '
                  'for i in 0..2 { for j in 0..2 { if (f[i, j]) { t += 1 + i * 2 + j; } } } return t; }',
    'rank2_across_functions': 'I64[3, 3] grid;\nvoid put(I64 i, I64 j, I64 v) { grid[i, j] = v; }\n'
                              'I64 get(I64 i, I64 j) { return grid[i, j]; }\n'
                              'export I64 run() { put(2, 1, 9); put(0, 2, 4); return get(2, 1) * 10 + get(0, 2); }',
    'rank2_view_argument': CORNER + 'I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];\nexport I64 run() { return corner(m); }',
    'rank2_inout_argument': 'I64[2, 2] m = [[1, 2], [3, 4]];\nvoid bump(inout I64[_, _] v) { v[0, 1] += 7; }\n'
                            'export I64 run() { bump(m); return m[0, 1]; }',
    'rank2_row': DIGITS + 'I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];\nexport I64 run() { return digits(m[1]); }',
    'rank2_copy': 'I64[2, 2] m = [[1, 2], [3, 4]];\nexport I64 run() { I64[2, 2] y = m; y[0, 0] = 9; m = y; '
                  'return m[0, 0] * 1000 + m[1, 1] * 100 + y[0, 1]; }',
    'rank2_reduction': 'I64[2, 3] m = [[1, 2, 3], [4, 5, 60]];\nexport I64 run() { return sum(m); }',
    'rank2_extents': 'I64[3, 4] m;\nexport I64 run() { return extent(m, 0) * 100 + extent(m, 1) * 10 + size(m); }',
    'rank2_slice': 'I64[2, 4] m = [[1, 2, 3, 4], [5, 6, 7, 8]];\nexport I64 run() { return sum(m[1, 1..3]); }',
    'rank2_transpose': CORNER + 'I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];\n'
                       'export I64 run() { m[0, 2] = 70; return corner(transpose(m)) * 100 + m[0, 2]; }',
    'rank2_with_rank1': 'I64[2] xs = [5, 6];\nI64 n = 2;\nI64[2, 2] m = [[1, 2], [3, 4]];\n'
                        'export I64 run() { m[1, 1] = xs[0] + n; xs[1] = m[0, 1]; return m[1, 1] * 100 + xs[1] * 10 + n; }',
    'rank2_unnamed': 'I64[2, 2] m;\nI64 other() { return 1; }\nexport I64 run() { return other(); }',
    'rank2_bounds_fault': 'I64[2, 2] m;\nexport I64 run() { I64 k = 2; return m[0, k]; }',
    'rank2_overflow_fault': 'I8[1, 1] m = [[127]];\nexport I64 run() { m[0, 0] += 1; return 0; }',
}
DIAGNOSTICS = {
    'call_item': 'I64 one() { return 1; }\nI64[2] xs = [one(), 2];\nexport I64 run() { return xs[0]; }',
    'variable_item': 'I64 a = 1;\nI64[2] xs = [a, 2];\nexport I64 run() { return xs[0]; }',
    'count_mismatch': 'I64[3] xs = [1, 2];\nexport I64 run() { return xs[0]; }',
    'element_range': 'U8[2] xs = [1, 256];\nexport I64 run() { return xs[0] as I64; }',
    'element_type': 'I64[2] xs = [1, true];\nexport I64 run() { return xs[0]; }',
    'item_fault': 'I64[1] xs = [9223372036854775807 + 1];\nexport I64 run() { return xs[0]; }',
    'scalar_initializer': 'I64[2] xs = 5;\nexport I64 run() { return xs[0]; }',
    'rank2_row_count': 'I64[2, 2] m = [[1, 2], [3]];\nexport I64 run() { return m[0, 0]; }',
    'rank2_flat_items': 'I64[2, 2] m = [1, 2, 3, 4];\nexport I64 run() { return m[0, 0]; }',
    'rank2_parameter_named_alike': 'I64[2, 2] m;\nI64 f(I64 m) { return m + 1; }\nexport I64 run() { return f(4); }',
    'rank2_local_named_alike': 'I64[2, 2] m;\nexport I64 run() { I64 m = 1; return m; }',
}
# cint_ref runs these; cintc refuses them with C9102 at the token it names (OQ-233): the
# declaration's name for more than 65,536 bytes of elements, the extent that is not a
# literal, or the imported name.
BOUNDARIES = {
    'rank1_bytes': ('I64[8193] big;\nexport I64 run() { return big[1]; }', 'big;'),
    'rank2_bytes': ('I64[100, 100] big;\nexport I64 run() { return big[1, 0]; }', 'big;'),
    'rank2_named_extent': ('const I64 N = 2;\nI64[N, 2] m;\nexport I64 run() { return m[1, 0]; }', 'N, 2]'),
    'imported_array': ('export I64 run() { return records.shared[1]; }', 'shared['),
}
LIBRARY = 'export I64 first(in I64[_] xs) { return xs[0]; }\nexport I64[2] shared = [7, 8];\n'


def boundaries(exe: pathlib.Path, out: pathlib.Path, cases=None, library=None) -> int:
    cases = BOUNDARIES if cases is None else cases
    library = LIBRARY if library is None else library
    failed = 0
    for name, (body, at) in cases.items():
        directory = out / name
        directory.mkdir(parents=True)
        source = directory / 'main.ci'
        text = 'import records;\n' + body
        source.write_bytes(text.encode('ascii'))
        (directory / 'records.ci').write_bytes(library.encode('ascii'))
        ref = subprocess.run([sys.executable, '-B', '-m', 'cint_ref', 'run', str(source), '--entry', 'run',
                              '--path', 'main.ci', '--format', '2'], cwd=ROOT / 'ref', capture_output=True, text=True,
                             timeout=120)
        emitted = subprocess.run([str(exe), '--json', 'emit-c', '--root', str(directory), '--out',
                                  str(directory / 'out'), 'records.ci', 'main.ci'], cwd=ROOT, capture_output=True,
                                 text=True, timeout=120)
        diag = iv.cc.b1_diagnostic(emitted.stderr)
        lines = text.splitlines()
        row = next(i for i, line in enumerate(lines) if at in line)
        position = 'main.ci:%d:%d' % (row + 1, lines[row].index(at) + 1)
        ok = (ref.returncode == 0 and 'outcome value\n' in ref.stdout and emitted.returncode == 2 and
              diag[:3] == ['outcome compile-error', 'diagnostic.code C9102', 'diagnostic.position ' + position])
        (directory / 'result.json').write_text(json.dumps(dict(reference=ref.stdout, stderr=emitted.stderr,
                                                               exit=emitted.returncode, passed=ok), indent=2) + '\n',
                                               encoding='utf-8')
        print(name + ': ' + ('pass' if ok else 'FAIL'), flush=True)
        failed += not ok
    return failed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--exe', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path)
    parser.add_argument('--only', action='append')
    known, _ = parser.parse_known_args()
    status = runner.main(VALUES, DIAGNOSTICS, {}, library=LIBRARY, label='module-level arrays')
    if not known.only:   # the boundary cases run with the whole set
        status = 1 if boundaries(known.exe, known.out / 'boundaries') else status
    raise SystemExit(status)
