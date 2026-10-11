"""Compares module-level Handle variables (decision on OQ-242; SPEC-03 M-20a; rt/OPEN.md
RT-OQ-50) with cint_ref: handles of a pool slot, of an arena allocation and of a struct pool,
null at start, read and written by several functions, passed as arguments, faulting when stale
or null; the diagnostics cint_ref gives at them; and the forms cintc refuses with C9102 that
cint_ref runs (an exported handle, an array of handles, a handle of another module). The record
layout table must type a handle field of a record, exported or not, as a handle."""
import argparse
import json
import pathlib
import subprocess
import sys

import import_views as iv
import named_constructors as runner

ROOT = pathlib.Path(__file__).resolve().parents[2]

POOL = 'Pool(I64, 2) counts;\n'
VALUES = {
    'null_at_start': POOL + 'Handle(I64) h;\nexport I64 run() { h = counts.insert(4); return counts[h]; }',
    'pool_slot': POOL + 'Handle(I64) h;\nvoid put(I64 v) { h = counts.insert(v); }\n'
                 'export I64 run() { put(6); counts[h] += 1; return counts[h]; }',
    'arena_allocation': 'Arena(I64, 8) scratch;\nHandle(I64[]) row;\n'
                        'export I64 run() { row = scratch.alloc(3); for i in 0..3 { scratch[row][i] = i + 1; } '
                        'return sum(scratch[row]); }',
    'struct_pool': 'struct Node { I64 v; Handle(Node) next; }\nPool(Node, 3) nodes;\nHandle(Node) head;\n'
                   'void push(I64 v) { head = nodes.insert(Node(v = v, next = head)); }\n'
                   'export I64 run() { push(1); push(2); return nodes[head].v * 10 + nodes[nodes[head].next].v; }',
    'argument': POOL + 'Handle(I64) h;\nI64 at(in Handle(I64) k) { return counts[k]; }\n'
                'export I64 run() { h = counts.insert(9); return at(h); }',
    'result': POOL + 'Handle(I64) h;\nHandle(I64) make(I64 v) { return counts.insert(v); }\n'
              'export I64 run() { h = make(5); return counts[h]; }',
    'local_copy': POOL + 'Handle(I64) h;\nexport I64 run() { h = counts.insert(2); Handle(I64) k = h; '
                  'counts[k] = 7; return counts[h]; }',
    'reinsert': POOL + 'Handle(I64) h;\nexport I64 run() { h = counts.insert(1); counts.remove(h); '
                'h = counts.insert(3); return counts[h]; }',
    'with_scalars': POOL + 'I64 n = 2;\nHandle(I64) h;\nI64[2] xs = [5, 6];\n'
                    'export I64 run() { h = counts.insert(n); n += counts[h]; return n * 10 + xs[1]; }',
    'stale_fault': POOL + 'Handle(I64) h;\nI64 seen = 0;\nexport I64 run() { h = counts.insert(1); '
                   'counts.remove(h); seen = 1; return counts[h]; }',
    'null_fault': POOL + 'Handle(I64) h;\nexport I64 run() { return counts[h]; }',
    'reset_fault': 'Arena(I64, 4) scratch;\nHandle(I64[]) row;\nexport I64 run() { row = scratch.alloc(2); '
                   'scratch.reset(); return scratch[row][0]; }',
    'unused': POOL + 'Handle(I64) h;\nexport I64 run() { return 1; }',
    'exported_record': 'export struct Node { I64 v; Handle(Node) next; }\nPool(Node, 3) nodes;\nHandle(Node) head;\n'
                       'export I64 run() { head = nodes.insert(Node(v = 4, next = head)); '
                       'head = nodes.insert(Node(v = 5, next = head)); return nodes[head].v * 10 + '
                       'nodes[nodes[head].next].v; }',
}
# The root's record layout table types Node's fields I64 and CINT_TAG_HANDLE (rt/OPEN.md RT-OQ-50), not
# Bool, for a record private to the root and an exported one: lines the emitted main.c must hold.
CATALOG = ('struct_pool', 'exported_record')
FIELDS = ('1u, {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u}},', '1u, {CINT_TAG_HANDLE, 0u, 0u, 0u, 0u, 0u, 0u}},')
DIAGNOSTICS = {
    'call_initializer': POOL + 'Handle(I64) h = counts.insert(1);\nexport I64 run() { return counts[h]; }',
    'literal_initializer': 'Handle(I64) h = 0;\nexport I64 run() { return 1; }',
    'compared': POOL + 'Handle(I64) h;\nHandle(I64) k;\nexport I64 run() { if (h == k) { return 1; } return 0; }',
    'element_type': POOL + 'Handle(I64[]) h;\nexport I64 run() { h = counts.insert(3); return 0; }',
    'constant': 'const Handle(I64) h;\nexport I64 run() { return 0; }',
}
# cint_ref runs these; cintc refuses them with C9102 at the text named, in the file named: an
# exported handle and an array of handles have no form in the declaration table or the module
# state (the gate, at the type), and only the root module has memory (lowering, at the name;
# compiler/OPEN.md CINTC-OQ-64 items 2, 12 and 14).
BOUNDARIES = {
    'exported': (POOL + 'export Handle(I64) h;\nexport I64 run() { h = counts.insert(3); return counts[h]; }', '',
                 'main.ci', 'Handle(I64) h'),
    'array_of_handles': ('Handle(I64)[2] hs;\nexport I64 run() { return 0; }', '', 'main.ci', 'Handle(I64)[2]'),
    'other_module': ('export I64 run() { return records.one(); }',
                     'Handle(I64) spare;\nexport I64 one() { return 1; }\n', 'records.ci', 'spare;'),
}
LIBRARY = 'export I64 first(in I64[_] xs) { return xs[0]; }\n'


def boundaries(exe: pathlib.Path, out: pathlib.Path) -> int:
    failed = 0
    for name, (body, library, where, at) in BOUNDARIES.items():
        directory = out / name
        directory.mkdir(parents=True)
        source = directory / 'main.ci'
        text = 'import records;\n' + body
        source.write_bytes(text.encode('ascii'))
        (directory / 'records.ci').write_bytes((library or LIBRARY).encode('ascii'))
        ref = subprocess.run([sys.executable, '-B', '-m', 'cint_ref', 'run', str(source), '--entry', 'run',
                              '--path', 'main.ci', '--format', '2'], cwd=ROOT / 'ref', capture_output=True, text=True,
                             timeout=120)
        emitted = subprocess.run([str(exe), '--json', 'emit-c', '--root', str(directory), '--out',
                                  str(directory / 'out'), 'records.ci', 'main.ci'], cwd=ROOT, capture_output=True,
                                 text=True, timeout=120)
        diag = iv.cc.b1_diagnostic(emitted.stderr)
        lines = (directory / where).read_text(encoding='ascii').splitlines()
        row = next(i for i, line in enumerate(lines) if at in line)
        position = '%s:%d:%d' % (where, row + 1, lines[row].index(at) + 1)
        ok = (ref.returncode == 0 and 'outcome value\n' in ref.stdout and emitted.returncode == 2 and
              diag[:3] == ['outcome compile-error', 'diagnostic.code C9102', 'diagnostic.position ' + position])
        (directory / 'result.json').write_text(json.dumps(dict(reference=ref.stdout, stderr=emitted.stderr,
                                                               exit=emitted.returncode, passed=ok), indent=2) + '\n',
                                               encoding='utf-8')
        print(name + ': ' + ('pass' if ok else 'FAIL'), flush=True)
        failed += not ok
    return failed


def catalog(out: pathlib.Path, names) -> int:
    failed = 0
    for name in names:
        text = iv.golden_suite.output_set(out / name / 'out')['main.c'].decode('ascii')
        missing = [line for line in FIELDS if line not in text]
        ok = not missing and 'CINT_TAG_BOOL' not in text
        print(name + ' catalog: ' + ('pass' if ok else 'FAIL ' + repr(missing)), flush=True)
        failed += not ok
    return failed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--exe', type=pathlib.Path)
    parser.add_argument('--out', type=pathlib.Path)
    parser.add_argument('--only', action='append')
    known, _ = parser.parse_known_args()
    status = runner.main(VALUES, DIAGNOSTICS, {}, library=LIBRARY, label='module-level handles')
    if catalog(known.out, [name for name in CATALOG if not known.only or name in known.only]):
        status = 1
    if not known.only:   # the boundary cases run with the whole set
        status = 1 if boundaries(known.exe, known.out / 'boundaries') else status
    raise SystemExit(status)
