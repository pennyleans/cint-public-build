"""Compares imported named constructor binding, values, and diagnostics with cint_ref."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

import import_views as iv

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main(values=None, diagnostics=None, boundaries=None, library=None, label='named constructors'):
    values = VALUES if values is None else values
    diagnostics = DIAGNOSTICS if diagnostics is None else diagnostics
    boundaries = BOUNDARIES if boundaries is None else boundaries
    library = LIBRARY if library is None else library
    parser = argparse.ArgumentParser(description=label)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--leg', default='msvc', choices=('msvc', 'gcc', 'clang'))
    parser.add_argument('--emit-only', action='store_true')
    parser.add_argument('--only', action='append', choices=(*values, *diagnostics, *boundaries))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    rows, commands = [], []

    def call(argv, cwd, env=None):
        argv = list(map(str, argv))
        result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=120)
        row = dict(command=argv, cwd=str(cwd), exit=result.returncode,
                   stdout=result.stdout, stderr=result.stderr)
        commands.append(row)
        (args.out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        return row

    class RecordedToolchain(iv.cc.Toolchain):
        def run(self, command, cwd, what):
            result = call(command, cwd, self.env)
            if result['exit'] != 0:
                raise iv.cc.BuildError(what + ': ' + result['stdout'] + result['stderr'])
            return subprocess.CompletedProcess(command, result['exit'], result['stdout'], result['stderr'])

    tc, runtime, harness = None, None, None
    for name, body in {**values, **diagnostics, **boundaries}.items():
        if args.only and name not in args.only:
            continue
        directory = args.out / name
        directory.mkdir()
        source = directory / 'main.ci'
        source.write_bytes(('import records;\n' + body).encode('ascii'))
        (directory / 'records.ci').write_bytes(library.encode('ascii'))
        ref = call([sys.executable, '-B', '-m', 'cint_ref', 'run', source, '--entry', 'run',
                    '--path', 'main.ci', '--format', '2'], ROOT / 'ref')
        emitted = call([args.exe, '--json', 'emit-c', '--root', directory, '--out', directory / 'out',
                        'records.ci', 'main.ci'], ROOT)
        row = dict(case=name, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                   reference=ref, emit=emitted, passed=False)
        if name in boundaries:
            diag = iv.cc.b1_diagnostic(emitted['stderr'])
            last = source.read_text().splitlines()[-1]
            position = 'main.ci:%d:%d' % (len(source.read_text().splitlines()), last.index('value =') + 1)
            row['boundary'] = 'BOOT-01 args admits named arguments only in constructors'
            row['passed'] = (ref['exit'] == 0 and 'return I64 7\n' in ref['stdout'] and
                             'fuel-consumed 2\n' in ref['stdout'] and emitted['exit'] == 2 and
                             diag == ['outcome compile-error', 'diagnostic.code C9102',
                                      'diagnostic.position ' + position])
        elif name in diagnostics:
            expected = [line for line in ref['stdout'].splitlines()
                        if line.startswith(('outcome ', 'diagnostic.'))]
            row['passed'] = (ref['exit'] == 0 and 'outcome compile-error' in expected and
                             emitted['exit'] == 2 and iv.cc.b1_diagnostic(emitted['stderr']) == expected)
        elif ref['exit'] == 0 and any('outcome ' + kind + '\n' in ref['stdout']
                                     for kind in ('value', 'fault')) and emitted['exit'] == 0:
            row['passed'] = True
            if not args.emit_only:
                if tc is None:
                    tc = RecordedToolchain(args.leg, '0', 'portable', False)
                    native = args.out / 'native'; native.mkdir()
                    runtime = tc.compile([ROOT / 'rt/cint_rt.c'], native, 'constructor runtime')
                    objects = tc.compile([ROOT / 'harness/cint_harness.c'], native, 'constructor harness')
                    harness = tc.link_exe(objects + runtime, native / 'harness', 'constructor harness')
                sources = []
                for relative, content in iv.golden_suite.output_set(directory / 'out').items():
                    if relative.endswith('.c'):
                        path = directory / relative; path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(content); sources.append(path)
                try:
                    objects = tc.compile(sources, directory, name)
                    native_library = tc.link_shared(objects + runtime, directory / name, name)
                    case_list = directory / 'cases.txt'; case_list.write_bytes(b'main run\n')
                    row['run'] = call([harness, native_library, '--cases', case_list, '--format', '2'], directory, tc.env)
                    result = row['run']
                    row['passed'] = (result['exit'] == 0 and not result['stderr'] and
                                     iv.observed(ref['stdout']) == iv.observed(result['stdout']))
                except iv.cc.BuildError as error:
                    row.update(passed=False, build_error=str(error))
        rows.append(row)
        (args.out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
        print(name + ': ' + ('pass' if row['passed'] else 'FAIL'), flush=True)
    passed = sum(row['passed'] for row in rows)
    print('%s: %d of %d pass' % (label, passed, len(rows)))
    return 0 if passed == len(rows) else 1

LIBRARY = 'export struct R { I64 first; I64 second; }\nexport struct Other { I64 first; I64 second; }\nexport struct Leaf { I64 value; }\nexport struct Outer { Leaf item; I64 tail; }\nexport struct Flag { Bool active; I64 value; }\nstruct Hidden { I64 value; }\nexport const I64 TOKEN = 7;\nconst I64 SECRET = 9;\nexport I64 ordinary(I64 value) { return value; }\nI64 private_fn(I64 value) { return value; }\n'

VALUES = {'positional_control': 'export I64 run() { records.R r = records.R(7, 2); return r.first * 10 + r.second; }\n',
 'local_named_control': 'struct L { I64 first; I64 second; }\n'
                        'export I64 run() { L r = L(second = 2, first = 7); return r.first * 10 + r.second; }\n',
 'named': 'export I64 run() { records.Leaf r = records.Leaf(value = 7); return r.value; }\n',
 'reordered': 'export I64 run() { records.R r = records.R(second = 2, first = 7); return r.first * 10 + r.second; '
              '}\n',
 'mixed': 'export I64 run() { records.R r = records.R(7, second = 2); return r.first * 10 + r.second; }\n',
 'alias': 'import records as other;\n'
          'export I64 run() { records.R r = other.R(second = 2, first = 7); return r.first * 10 + r.second; }\n',
 'nested': 'export I64 run() { records.Outer r = records.Outer(tail = 2, item = records.Leaf(value = 7)); return '
           'r.item.value * 10 + r.tail; }\n',
 'bool': 'export I64 run() { records.Flag r = records.Flag(value = 7, active = true); if (r.active) { return '
         'r.value; } return 0; }\n',
 'written_order': 'I64 step(inout I64[1] a) { a[0] += 1; return a[0]; }\n'
                  'export I64 run() { I64[1] a; records.R r = records.R(second = step(a), first = step(a)); '
                  'return r.first * 10 + r.second; }\n',
 'record_capture': 'I64 mutate(inout records.Leaf[1] a) { a[0].value = 9; return 0; }\n'
                   'export I64 run() { records.Leaf[1] a; a[0].value = 1; records.Outer r = records.Outer(item = '
                   'a[0], tail = mutate(a)); return r.item.value; }\n',
 'early_bounds': 'I64 mutate(inout records.Leaf[1] a) { a[0].value = 9; return 0; }\n'
                 'export I64 run() { records.Leaf[1] a; I64 idx = 1; records.Outer r = records.Outer(item = '
                 'a[idx], tail = mutate(a)); return r.item.value; }\n'}

DIAGNOSTICS = {'duplicate': 'export I64 run() { records.R r = records.R(first = 7, first = 2); return 0; }\n',
 'positional_then_duplicate': 'export I64 run() { records.R r = records.R(7, first = 2); return 0; }\n',
 'unknown_field': 'export I64 run() { records.R r = records.R(missing = 7, first = 2); return 0; }\n',
 'missing_field': 'export I64 run() { records.R r = records.R(first = 7); return 0; }\n',
 'positional_after_named': 'export I64 run() { records.R r = records.R(second = 2, 7); return 0; }\n',
 'argument_type': 'export I64 run() { records.R r = records.R(first = true, second = 2); return 0; }\n',
 'typename_argument': 'export I64 run() { records.R r = records.R(first = I64, second = 2); return 0; }\n',
 'private_type': 'export I64 run() { return records.Hidden(value = absent); }\n',
 'private_before_kind': 'export I64 run() { return records.SECRET(value = absent); }\n',
 'wrong_kind': 'export I64 run() { return records.TOKEN(value = absent); }\n',
 'missing_type': 'export I64 run() { return records.Missing(value = absent); }\n',
 'nominal_mismatch': 'export I64 run() { records.Other r = records.R(first = 7, second = 2); return 0; }\n',
 'binding_before_value': 'export I64 run() { records.R r = records.R(first = absent, unknown = 2); return 0; }\n'}

BOUNDARIES = {'ordinary_named_boundary': 'export I64 run() { return records.ordinary(value = 7); }\n',
 'local_named_boundary': 'I64 ordinary(I64 value) { return value; }\n'
                         'export I64 run() { return ordinary(value = 7); }\n'}


if __name__ == '__main__':
    raise SystemExit(main())
