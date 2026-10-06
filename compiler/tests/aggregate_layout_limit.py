"""Checks the cpu-c17 record layout limit without allocating giant values."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def powers(n, scalar='U8'):
    return 'struct P0 { %s value; }\n' % scalar + ''.join(
        'struct P%d { P%d a; P%d b; }\n' % (j, j-1, j-1) for j in range(1, n+1))


def cases():
    base = powers(31)
    maximum = 'struct Max { ' + ''.join('P%d a%d; ' % (j, j) for j in range(31, -1, -1)) + '}\n'
    tail = 'struct TailPad { U16 head; ' + ''.join('P%d a%d; ' % (j, j) for j in range(31, 1, -1)) + 'U8 last; }\n'
    yield 'maximum', base + maximum, None, None
    yield 'sum_over', base + maximum + 'struct SumOver { Max a; U8 b; }\n', 'SumOver', None
    yield 'offset_align_over', base + maximum + 'struct OffsetOver { Max a; U16 b; }\n', 'OffsetOver', None
    yield 'tail_align_over', base + tail, 'TailPad', None
    yield 'doubling_60', powers(60, 'I64'), 'P29', None
    yield 'scalar_array_product', 'struct Product { I64[512] items; }\n', None, None
    yield 'record_array_product_fit', powers(30) + 'struct ArrayFit { P30[3] items; }\n', None, None
    yield 'record_array_product_over', powers(30) + 'struct ArrayOver { P30[4] items; }\n', 'ArrayOver', None
    yield 'imported_record_array_fit', 'import library;\nstruct ImportedFit { library.Big[3] items; }\n', None, powers(30) + 'export struct Big { P30 item; }\n'
    yield 'imported_record_array_over', 'import library;\nstruct ImportedOver { library.Big[4] items; }\n', 'ImportedOver', powers(30) + 'export struct Big { P30 item; }\n'
    yield 'imported_over', 'import library;\nstruct ImportedOver { library.Big a; library.Big b; }\n', 'ImportedOver', powers(31) + 'export struct Big { P31 item; }\n'
    yield 'imported_fit', 'import library;\nstruct ImportedFit { library.Big a; U8 b; }\n', None, powers(30) + 'export struct Big { P30 item; }\n'


def run(argv, cwd):
    r = subprocess.run(list(map(str, argv)), cwd=cwd, capture_output=True, text=True)
    return dict(command=list(map(str, argv)), cwd=str(cwd), exit=r.returncode, stdout=r.stdout, stderr=r.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    results = []
    for name, source, offending, library in cases():
        directory = args.out / name
        directory.mkdir()
        source += 'export I64 run() { return 0; }\n'
        path = directory / 'main.ci'
        path.write_bytes(source.encode())
        modules = ['main.ci']
        if library:
            (directory / 'library.ci').write_bytes(library.encode())
            modules.insert(0, 'library.ci')
        row = dict(case=name, sources={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.glob('*.ci')})
        row['reference'] = run([sys.executable, '-B', '-m', 'cint_ref', 'run', path, '--entry', 'run', '--path', 'main.ci', '--format', '2'], ROOT / 'ref')
        row['emit'] = run([args.exe, '--json', 'emit-c', '--root', directory, '--out', directory / 'out', *modules], ROOT)
        good = row['reference']['exit'] == 0 and 'return I64 0\n' in row['reference']['stdout']
        if offending:
            line = next(j for j, line in enumerate(source.splitlines(), 1) if line.startswith('struct '+offending+' '))
            row['cpu_c17_expected'] = dict(code='C9001', line=line, column=8, limit_identifier=31, limit=4294967295)
            try:
                diag = json.loads(row['emit']['stderr'])
            except json.JSONDecodeError:
                diag = {}
            good = good and row['emit']['exit'] == 2 and diag.get('code') == 'C9001' and diag.get('line') == line and diag.get('column') == 8 and 'LIM_RECORD_BYTES' in diag.get('message', '')
        else:
            good = good and row['emit']['exit'] == 0
        row['passed'] = good
        results.append(row)
        (args.out / 'results.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8', newline='\n')
        print(name+': '+('pass' if good else 'FAIL'), flush=True)
    passed = sum(row['passed'] for row in results)
    print('aggregate layout: %d of %d pass' % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
