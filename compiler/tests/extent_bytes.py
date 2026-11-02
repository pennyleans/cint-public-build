"""Compares the output set of a program whose array extents are literals with that of the same
program whose extents are constant expressions: each pair keeps every declaration,
and differs only in the text of the extents, so `cint emit-c` must write the same bytes."""
import argparse
import pathlib
import subprocess
import sys

import aggregate_values as av

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONSTANTS = 'const I64 N = 3;\nconst I64 A = 2;\nconst I64 B = 256;\n'
# Each case: the source with %s for its extents, the literal text, the constant text.
PAIRS = {
    'rank1': ('I64[%s] xs;\nexport I64 run() { xs[2] = 5; return xs[2] + len(xs); }\n', ('3',), ('N',)),
    'product': ('I64[%s] xs;\nexport I64 run() { xs[5] = 5; return xs[5] + len(xs); }\n', ('6',), ('N * A',)),
    'expression_on_the_line': ('I64[%s] xs; I64 k = 1;\nexport I64 run() { return xs[k] + len(xs) + k; }\n', ('7',), ('(N + 4)',)),
    'initialized': ('I64[%s] xs = [1, 2, 3];\nexport I64 run() { return xs[2]; }\n', ('3',), ('N',)),
    'bounds_fault': ('I64[%s] xs;\nexport I64 run() { I64 k = 3; return xs[k]; }\n', ('3',), ('N',)),
    'above_code': ('U8[%s] xs;\nexport I64 run() { xs[65534] = 5; return len(xs); }\n', ('65535',), ('B * B - 1',)),
    'most': ('I64[%s] xs;\nexport I64 run() { xs[65535] = 5; return len(xs); }\n', ('65536',), ('B * B',)),
    'rank2': ('I32[%s, %s] xs;\nexport I64 run() { xs[2, 1] = 5; return xs[2, 1] as I64 + size(xs); }\n',
              ('3', '2'), ('N', 'A')),
    'rank4': ('U8[%s, %s, %s, %s] xs;\nexport I64 run() { xs[1, 2, 1, 0] = 5; return size(xs); }\n',
              ('2', '3', '2', '1'), ('A', 'N', 'A', 'A - 1')),
    'rank2_view': ('I64[%s, %s] xs;\nI64 f(in I64[_, _] v) { return v[1, 1]; }\n'
                   'export I64 run() { xs[1, 1] = 5; return f(xs); }\n', ('3', '2'), ('N', 'A')),
    'size_parameter': ('I64[%s] xs;\nI64 f[n](inout I64[n] v) { v[0] = 1; return len(v); }\n'
                      'export I64 run() { return f(xs); }\n', ('3',), ('N',)),
    'field': ('struct S { I64[%s] h; }\nexport I64 run() { S s; s.h[2] = 5; return s.h[2] + len(s.h); }\n',
              ('3',), ('N',)),
    'field_product': ('struct S { I64[%s] h; I32[%s, %s] g; }\nS kept;\n'
                      'export I64 run() { kept.g[2, 1] = 5; return kept.h[5] + len(kept.h); }\n',
                      ('6', '3', '2'), ('N * A', 'N', 'A')),
    'field_of_structs': ('struct L { I64 v; }\nstruct S { L[%s] h; }\n'
                         'export I64 run() { S s; s.h[2].v = 5; return s.h[2].v; }\n', ('3',), ('N',)),
}


def emit(exe: pathlib.Path, directory: pathlib.Path, source: str) -> dict:
    directory.mkdir(parents=True)
    (directory / 'main.ci').write_bytes(source.encode('ascii'))
    done = subprocess.run([str(exe), '--json', 'emit-c', '--root', str(directory), '--out', str(directory / 'out'),
                           'main.ci'], cwd=ROOT, capture_output=True, text=True, timeout=120)
    if done.returncode != 0:
        raise RuntimeError(done.stderr[:400])
    return av.golden_suite.output_set(directory / 'out')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    failed = 0
    for name, (source, literal, named) in PAIRS.items():
        try:
            left = emit(args.exe, args.out / name / 'literal', CONSTANTS + source % literal)
            right = emit(args.exe, args.out / name / 'named', CONSTANTS + source % named)
            differ = sorted(k for k in set(left) | set(right) if left.get(k) != right.get(k))
        except (RuntimeError, ValueError) as error:
            differ = [str(error)]
        print('%s: %s' % (name, 'pass (%d files)' % len(left) if not differ else 'FAIL ' + ', '.join(differ)), flush=True)
        failed += bool(differ)
    print('extent bytes: %d of %d pass' % (len(PAIRS) - failed, len(PAIRS)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
