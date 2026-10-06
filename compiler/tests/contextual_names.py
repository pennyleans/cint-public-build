"""Compares contextual names with the reference, including native values and fuel."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

import aggregate_values as av
from contextual_names_cases import CASES

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(command, cwd, env=None):
    command = list(map(str, command))
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
    return dict(command=command, cwd=str(cwd), exit=result.returncode,
                stdout=result.stdout, stderr=result.stderr)


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
        rt = tc.compile([ROOT / 'rt/cint_rt.c'], native, 'names-runtime')
        objects = tc.compile([ROOT / 'harness/cint_harness.c'], native, 'names-harness')
        harness = tc.link_exe(objects + rt, native / 'harness', 'names-harness')
    rows = []
    for name, files in CASES.items():
        directory = args.out / name
        directory.mkdir()
        for rel, text in files.items():
            (directory / rel).write_bytes(text.encode('ascii'))
        row = dict(case=name, sources={rel: hashlib.sha256(text.encode('ascii')).hexdigest()
                                      for rel, text in files.items()})
        row['reference'] = run([sys.executable, '-B', '-m', 'cint_ref', 'run', directory / 'main.ci',
                                '--entry', 'run', '--path', 'main.ci', '--format', '2'], ROOT / 'ref')
        modules = sorted(rel for rel in files if rel != 'main.ci') + ['main.ci']
        row['emit'] = run([args.exe, '--json', 'emit-c', '--root', directory,
                           '--out', directory / 'out', *modules], ROOT)
        ref, emitted = row['reference'], row['emit']
        wanted = [line for line in ref['stdout'].splitlines()
                  if line.startswith(('outcome ', 'diagnostic.'))]
        if 'outcome compile-error' in wanted:
            row['passed'] = (ref['exit'] == 0 and emitted['exit'] == 2 and
                             av.cc.b1_diagnostic(emitted['stderr']) == wanted)
        else:
            row['passed'] = ref['exit'] == 0 and 'outcome value' in wanted and emitted['exit'] == 0
            if row['passed'] and tc:
                sources = []
                for rel, data in av.golden_suite.output_set(directory / 'out').items():
                    if rel.endswith('.c'):
                        target = directory / rel
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(data)
                        sources.append(target)
                try:
                    objects = tc.compile(sources, directory, 'names-case')
                    library = tc.link_shared(objects + rt, directory / name, 'names-case')
                    listing = directory / 'cases.txt'
                    listing.write_bytes(b'main run\n')
                    row['run'] = run([harness, library, '--cases', listing, '--format', '2'], ROOT, tc.env)
                    result = row['run']
                    row['passed'] = (result['exit'] == 0 and not result['stderr'] and
                                     av.observed(ref['stdout']) == av.observed(result['stdout']))
                except av.cc.BuildError as error:
                    row['build_error'] = str(error)
                    row['passed'] = False
        rows.append(row)
        (args.out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
        print(name + ': ' + ('pass' if row['passed'] else 'FAIL'), flush=True)
    if tc:
        (args.out / 'native-commands.json').write_text(json.dumps(tc.commands, indent=2) + '\n', encoding='utf-8')
    passed = sum(row['passed'] for row in rows)
    print('contextual names: %d of %d pass' % (passed, len(rows)))
    return 0 if passed == len(rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
