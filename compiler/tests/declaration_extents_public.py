"""Replays public record and view hosts with checked constant extents."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import aggregate_values as av
import constant_extents as ext
import import_views

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--leg', choices=('msvc', 'gcc', 'clang'), default='msvc')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    tc = av.RecordedToolchain(args.leg)
    runtime = tc.compile([ROOT / 'rt/cint_rt.c'], args.out, 'extent-public-runtime')
    golden = ROOT / 'compiler/tests/golden'
    records = {p: (golden / p).read_text() for p in ('record_lib.ci', 'record_mid.ci', 'public_records.ci')}
    records['public_records.ci'] = records['public_records.ci'].replace('Bool[2]', 'Bool[N + 1]') + '\nconst I64 N = 1;\n'
    records['record_lib.ci'] = records['record_lib.ci'].replace('Bool[3]', 'Bool[THREE]') + '\nconst I64 THREE = 1 + 2;\n'
    views = {'records.ci': import_views.LIBRARY, 'twin.ci': 'export struct R { Bool active; I64 value; }\n',
             'main.ci': (ROOT / 'compiler/tests/import_views_public.ci').read_text().replace('records.R[2]', 'records.R[N]') + '\nconst I64 N = 1 + 1;\n'}
    rows = []
    for name, sources, host, entry in (('records', records, 'public_records_host.c', 'public_records.ci'),
                                        ('views', views, 'import_views_host.c', 'main.ci')):
        directory = args.out / name
        directory.mkdir()
        for rel, data in sources.items():
            (directory / rel).write_bytes(data.encode())
        row = {'case': name, 'sources': {rel: hashlib.sha256((directory / rel).read_bytes()).hexdigest() for rel in sources},
               'host_sha256': hashlib.sha256((ROOT / 'compiler/tests' / host).read_bytes()).hexdigest(), 'passed': False}
        row['reference'] = ext.run([sys.executable, '-B', '-m', 'cint_ref', 'run', directory / entry, '--entry', 'aliases' if name == 'records' else 'run', '--path', entry, '--format', '2'], ROOT / 'ref')
        row['emit'] = ext.run([args.exe, '--json', 'emit-c', '--root', directory, '--out', directory / 'out', *sources], ROOT)
        if row['reference']['exit'] == 0 and 'outcome value\n' in row['reference']['stdout'] and row['emit']['exit'] == 0:
            emitted = []
            for rel, data in av.golden_suite.output_set(directory / 'out').items():
                if rel.endswith('.c'):
                    path = directory / rel
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                    emitted.append(path)
            try:
                objects = tc.compile(emitted, directory, name)
                host_objects = tc.compile([ROOT / 'compiler/tests' / host], directory, name + '-host')
                executable = tc.link_exe(objects + host_objects + runtime, directory / 'host', name + '-host')
                row['run'] = ext.run([executable], directory, tc.env)
                library = tc.link_shared(objects + runtime, directory / 'audit', name + '-audit')
                row['audit'] = av.cc.audit_program([tc.symbols(obj) for obj in objects], tc.exports(library), tc.symbols(runtime[0])[0], tc.leg)
                row['passed'] = row['run']['exit'] == 0 and not row['run']['stderr'] and not row['audit']
            except av.cc.BuildError as error:
                row['build_error'] = str(error)
        rows.append(row)
        (args.out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf8')
        (args.out / 'native-commands.json').write_text(json.dumps(tc.commands, indent=2) + '\n', encoding='utf8')
        print(name + ': ' + ('pass' if row['passed'] else 'FAIL'), flush=True)
    return 0 if all(row['passed'] for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
