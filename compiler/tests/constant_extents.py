"""Compares checked local extents, diagnostic faults, and native values."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

import aggregate_values as av

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = {'module_named': 'const I64 N = 2;\nexport I64 run() { I64[N] a; a[1] = 7; return a[1]; }\n',
 'module_forward': 'export I64 run() { I64[N] a; a[1] = 7; return a[1]; }\nconst I64 N = 2;\n',
 'module_dependency': 'const I64 N = M + 1;\n'
                      'const I64 M = 1;\n'
                      'export I64 run() { I64[N] a; a[1] = 7; return a[1]; }\n',
 'local_named': 'export I64 run() { const I64 N = 2; I64[N] a; a[1] = 7; return a[1]; }\n',
 'composed': 'export I64 run() { I64[(1 + 3) / 2] a; a[1] = 7; return a[1]; }\n',
 'hex': 'export I64 run() { I64[0x2] a; a[1] = 7; return a[1]; }\n',
 'binary': 'export I64 run() { I64[0b10] a; a[1] = 7; return a[1]; }\n',
 'character': "export I64 run() { I64['b' - 'a' + 1] a; a[1] = 7; return a[1]; }\n",
 'conversion': 'export I64 run() { I64[(2 as U8) as I64] a; a[1] = 7; return a[1]; }\n',
 'short_circuit': 'export I64 run() { I64[(false && (1 / 0 == 0)) ? 1 : 2] a; a[1] = 7; return a[1]; }\n',
 'conditional': 'export I64 run() { I64[true ? 2 : 1 / 0] a; a[1] = 7; return a[1]; }\n',
 'wrong_width': 'export I64 run() { I64[2 as U8] a; a[1] = 7; return a[1]; }\n',
 'wrong_bool': 'export I64 run() { I64[true] a; a[1] = 7; return a[1]; }\n',
 'nonconstant': 'I64 f(I64 n) { I64[n] a; return 0; }\nexport I64 run() { return f(2); }\n',
 'dynamic_size': 'I64 f[n](in I64[n] input) { I64[n] a; return 0; }\n'
                 'export I64 run() { I64[2] a; return f(a); }\n',
 'overflow': 'export I64 run() { I64[I64.max + 1] a; a[1] = 7; return a[1]; }\n',
 'divide_zero': 'export I64 run() { I64[1 / 0] a; a[1] = 7; return a[1]; }\n',
 'literal_overflow': 'export I64 run() { I64[9223372036854775808] a; a[1] = 7; return a[1]; }\n',
 'negative_literal_overflow': 'export I64 run() { I64[-9223372036854775809] a; a[1] = 7; return a[1]; }\n',
 'negative': 'export I64 run() { I64[-1] a; a[1] = 7; return a[1]; }\n',
 'cycle': 'const I64 N = M;\nconst I64 M = N;\nexport I64 run() { I64[N] a; a[1] = 7; return a[1]; }\n',
 'zero': 'export I64 run() { I64[0 + 0] a; return 4; }\n',
 'limit': 'export I64 run() { I64[256 * 2] a; a[511] = 7; return a[511]; }\n',
 'over_4096_bytes': 'export I64 run() { I64[512 + 1] a; return 4; }\n',
 'frame_arena': 'export I64 run() { I64[1000000 + 1] a; a[1000000] = 7; return a[1000000]; }\n',
 'frame_arena_full': 'export I64 run() { I64[2097152 + 1] a; return 4; }\n',
 'over_limit': 'export I64 run() { I64[65536 * 65536 + 1] a; return 4; }\n',
 'record': 'struct R { I64 v; }\nexport I64 run() { const I64 N = 2; R[N] a; a[1].v = 7; return a[1].v; }\n',
 'field': 'const I64 N = 2;\nstruct R { I64[N] a; }\nexport I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n',
 'fixed_view': 'const I64 N = 2;\n'
               'I64 f(in I64[N] a) { return a[1]; }\n'
               'export I64 run() { I64[2] a; a[1] = 7; return f(a); }\n',
 'imported': 'import ext;\nexport I64 run() { I64[ext.N] a; a[1] = 7; return a[1]; }\n',
 'imported_record': 'import ext;\nexport I64 run() { const I64 N = 2; ext.R[N] a; a[1].v = 7; return a[1].v; }\n',
 'imported_record_order': 'import ext;\nexport I64 run() { ext.Missing[1 / 0] a; return 0; }\n',
 'shadow_global': 'const I64 N = M;\n'
                  'const I64 M = 2;\n'
                  'export I64 run() { const I64 M = 3; I64[N] a; a[1] = 7; return a[1]; }\n',
 'unknown_type_order': 'export I64 run() { Missing[1 / 0] a; return 0; }\n',
 'signature_order': 'export I64 run() { I64[1 / 0] a; a[1] = 7; return a[1]; }\nvoid later(Missing arg) {}\n',
 'local_before_declaration': 'export I64 run() { I64[N] a; const I64 N = 2; return 0; }\n'}

# These reference outcomes remain outside the local constant-extent unit. B1 compiles
# dynamic_size and gives negative's C2015 since its box 09 work.
BOUNDARY = {'over_limit'}


def run(command, cwd, env=None):
    command = list(map(str, command))
    p = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
    return dict(command=command, cwd=str(cwd), exit=p.returncode, stdout=p.stdout, stderr=p.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--leg', default='msvc', choices=('msvc', 'gcc', 'clang'))
    parser.add_argument('--emit-only', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    tc = None
    if not args.emit_only:
        tc = av.RecordedToolchain(args.leg)
        native = args.out / 'native'
        native.mkdir()
        rt = tc.compile([ROOT / 'rt/cint_rt.c'], native, 'extent-runtime')
        objects = tc.compile([ROOT / 'harness/cint_harness.c'], native, 'extent-harness')
        harness = tc.link_exe(objects + rt, native / 'harness', 'extent-harness')
    rows = []
    for name, source in CASES.items():
        directory = args.out / name
        directory.mkdir()
        path = directory / (name + '.ci')
        path.write_bytes(source.encode())
        (directory / 'ext.ci').write_bytes(b'export const I64 N = 2;\nexport struct R { I64 v; }\n')
        modules = ['ext.ci', path.name] if 'import ext;' in source else [path.name]
        row = dict(case=name, source_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        row['reference'] = run([sys.executable, '-B', '-m', 'cint_ref', 'run', path, '--entry', 'run', '--path', path.name, '--format', '2'], ROOT / 'ref')
        row['emit'] = run([args.exe, '--json', 'emit-c', '--root', directory, '--out', directory / 'out', *modules], ROOT)
        ref = row['reference']
        emit = row['emit']
        wanted = [line for line in ref['stdout'].splitlines() if line.startswith(('outcome ', 'diagnostic.'))]
        if name in BOUNDARY:
            observed = av.cc.b1_diagnostic(emit['stderr'])
            row['boundary'] = 'constant extent unit excludes this source'
            row['passed'] = ref['exit'] == 0 and emit['exit'] == 2 and 'diagnostic.code C9102' in observed
        elif 'outcome compile-error' in wanted:
            row['passed'] = ref['exit'] == 0 and emit['exit'] == 2 and av.cc.b1_diagnostic(emit['stderr']) == wanted
        else:
            row['passed'] = ref['exit'] == 0 and emit['exit'] == 0
            if row['passed'] and tc:
                sources = []
                for rel, data in av.golden_suite.output_set(directory / 'out').items():
                    if rel.endswith('.c'):
                        target = directory / rel
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(data)
                        sources.append(target)
                try:
                    objects = tc.compile(sources, directory, 'extent-case')
                    library = tc.link_shared(objects + rt, directory / name, 'extent-case')
                    case_list = directory / 'cases.txt'
                    case_list.write_bytes((name + ' run\n').encode())
                    row['run'] = run([harness, library, '--cases', case_list, '--format', '2'], ROOT, tc.env)
                    result = row['run']
                    row['passed'] = result['exit'] == 0 and not result['stderr'] and av.observed(ref['stdout']) == av.observed(result['stdout'])
                except av.cc.BuildError as error:
                    row['build_error'] = str(error)
                    row['passed'] = False
        rows.append(row)
        (args.out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8', newline='\n')
        print(name + ': ' + ('pass' if row['passed'] else 'FAIL'), flush=True)
    if tc:
        (args.out / 'native-commands.json').write_text(json.dumps(tc.commands, indent=2) + '\n', encoding='utf-8', newline='\n')
    passed = sum(row['passed'] for row in rows)
    print('constant extents: %d of %d pass (%d boundary probes)' % (passed, len(rows), len(BOUNDARY)))
    return 0 if passed == len(rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
