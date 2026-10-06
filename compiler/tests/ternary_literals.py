"""Compares B1's balanced-ternary literals (SPEC-04 LS-24, LS-30) with cint_ref: values,
signs, range checks, constant conversions, and the low 64 bits of wide literals (OQ-191).

    python compiler/tests/ternary_literals.py --exe CINT --out DIR [--leg msvc|gcc|clang]

CINT is the `cint` command that tools/cint_bootstrap.py builds with B1. Each case is run by
`cint_ref run --format 2` and compiled by `cint emit-c`; a compile error must give
cint_ref's diagnostic lines, and a program must give its outcome, return, fuel, and fault
lines through the native harness.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

import aggregate_values as av

ROOT = pathlib.Path(__file__).resolve().parents[2]
WIDE = "N" + "P" * 2000
CASES = {
    'value': 'export I64 run() { return 0tP0N; }\n',
    'negative': 'export I64 run() { return 0tN0P; }\n',
    'negated': 'export I64 run() { return -0tN; }\n',
    'separators': 'export I64 run() { return 0tP_0_N + 0tN_N; }\n',
    'zero': 'export I64 run() { return 0t000 + -0t0; }\n',
    'i8_bounds': 'export I64 run() { I8 a = 0tPNNN0P; I8 b = 0tNPPPNP; return (a as I64) * 1000 + (b as I64); }\n',
    'i8_over': 'export I64 run() { I8 a = 0tPNNNPN; return a as I64; }\n',
    'u8_negative': 'export I64 run() { U8 a = 0tN; return a as I64; }\n',
    'u8_negated': 'export I64 run() { U8 a = -0tN; return a as I64; }\n',
    'i64_min': 'export I64 run() { return 0tNPNPNNN00NNN00PN0NPPNNP0N0N0P0P0NNP0PN00P; }\n',
    'u64_max': 'export U64 run() { return 0tPNNNN00N0P00N00NN0PPNNPPPNPNNPNPPNPP0NN0N0; }\n',
    'checked_conversion': 'export I64 run() { I8 a = 0tPPPPPP as I8; return a as I64; }\n',
    'wrap_conversion': 'export I64 run() { I8 a = 0tPPPPPP as% I8; return a as I64; }\n',
    'wide_wrap': 'export I64 run() { return 0t' + WIDE + ' as% I64; }\n',
    'wide_negated_wrap': 'export I64 run() { return -0t' + WIDE + ' as% I64; }\n',
    'runtime': 'export I64 run(I64 x) { return x * 0tPN + 0tN; }\n',
    'runtime_overflow': 'export I64 run() { I64 x = 0tPNPNPPP00PPP00NP0PNNPPN0P0P0N0N0PPN0NP0NP; return x + 0tP; }\n',
    'switch': 'export I64 run() {\n    I64 t = 0;\n    for x in 0..5 {\n        switch (x) {\n'
              '            case 0tP: t = t * 10 + 1;\n            case 0tPN..=0tP0: t = t * 10 + 2;\n'
              '            default: t = t * 10 + 9;\n        }\n    }\n    return t;\n}\n',
    'constant': 'const I64 K = 0tPPP * 0tN;\nexport I64 run() { return K; }\n',
    'static_assert': 'static_assert(0tP0N == 8 && -0tN == 1);\nexport I64 run() { return 0tP; }\n',
    'extent': 'export I64 run() { I64[0tP0N] a; a[7] = 0tPN; return a[7]; }\n',
    'index': 'export I64 run() { I64[4] a; a[0tPN] = 5; return a[2]; }\n',
    'bounds_fault': 'export I64 run() { I64[4] a; return a[0tPN0]; }\n',
}


def run(command, cwd, env=None):
    command = list(map(str, command))
    p = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
    return dict(command=command, cwd=str(cwd), exit=p.returncode, stdout=p.stdout, stderr=p.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--leg', default='msvc', choices=('msvc', 'gcc', 'clang'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    tc = av.RecordedToolchain(args.leg)
    native = args.out / 'native'
    native.mkdir()
    rt = tc.compile([ROOT / 'rt/cint_rt.c'], native, 'ternary-runtime')
    objects = tc.compile([ROOT / 'harness/cint_harness.c'], native, 'ternary-harness')
    harness = tc.link_exe(objects + rt, native / 'harness', 'ternary-harness')
    rows = []
    for name, source in CASES.items():
        directory = args.out / name
        directory.mkdir()
        path = directory / (name + '.ci')
        path.write_bytes(source.encode())
        row = dict(case=name, source_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        entry = ['--entry', 'run'] + (['--arg', 'I64 7'] if name == 'runtime' else [])
        row['reference'] = run([sys.executable, '-B', '-m', 'cint_ref', 'run', path, *entry,
                                '--path', path.name, '--format', '2'], ROOT / 'ref')
        row['emit'] = run([args.exe, '--json', 'emit-c', '--root', directory, '--out', directory / 'out',
                           path.name], ROOT)
        ref, emit = row['reference'], row['emit']
        wanted = [line for line in ref['stdout'].splitlines() if line.startswith(('outcome ', 'diagnostic.'))]
        if 'outcome compile-error' in wanted:
            row['passed'] = ref['exit'] == 0 and emit['exit'] == 2 and av.cc.b1_diagnostic(emit['stderr']) == wanted
        else:
            row['passed'] = ref['exit'] == 0 and emit['exit'] == 0
            if row['passed']:
                sources = []
                for rel, data in av.golden_suite.output_set(directory / 'out').items():
                    if rel.endswith('.c'):
                        target = directory / rel
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(data)
                        sources.append(target)
                try:
                    objects = tc.compile(sources, directory, 'ternary-case')
                    library = tc.link_shared(objects + rt, directory / name, 'ternary-case')
                    case_list = directory / 'cases.txt'
                    call = 'run I64 7' if name == 'runtime' else 'run'
                    case_list.write_bytes((name + ' ' + call + '\n').encode())
                    row['run'] = run([harness, library, '--cases', case_list, '--format', '2'], ROOT, tc.env)
                    result = row['run']
                    row['passed'] = (result['exit'] == 0 and not result['stderr']
                                     and av.observed(ref['stdout']) == av.observed(result['stdout']))
                except av.cc.BuildError as error:
                    row['build_error'] = str(error)
                    row['passed'] = False
        rows.append(row)
        (args.out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8', newline='\n')
        print(name + ': ' + ('pass' if row['passed'] else 'FAIL'), flush=True)
    (args.out / 'native-commands.json').write_text(json.dumps(tc.commands, indent=2) + '\n', encoding='utf-8',
                                                   newline='\n')
    passed = sum(row['passed'] for row in rows)
    print('ternary literals: %d of %d pass' % (passed, len(rows)))
    return 0 if passed == len(rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
