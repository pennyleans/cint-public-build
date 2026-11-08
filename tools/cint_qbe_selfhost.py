"""Q4 of the cpu-qbe backend (docs/design/cpu-qbe.md): a QBE-built compiler reproduces the
reference emitted compiler files.

python tools/cint_qbe_selfhost.py --cint /path/to/cint --out /external/directory [--qbe PATH]

1. The reference cint emits compiler/main.ci through cpu-qbe; QBE, as and ld (the toolchain of
   tools/cint_qbe_check.py) build those modules into a second cint, linked with the reference
   build's CLI and release runtime objects in place of its compiler object.
2. Both compilers emit main.ci as C17 and as QBE; each tree must equal the reference byte for
   byte, and the QBE tree must equal the one the second compiler was built from (a fixpoint).
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cint_qbe_check


def keys(path):
    """The lines of a toolchain file by key, in order."""
    out = {}
    for s in path.read_text().splitlines():
        k, _, v = s.partition(' ')
        out.setdefault(k, []).append(v)
    return out


def tree(stage):
    return {str(f.relative_to(stage)): f.read_bytes() for f in sorted(stage.rglob('*')) if f.is_file()}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--cint', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--qbe', type=Path, help='the QBE binary (default: qbe on PATH)')
    a = p.parse_args()
    if not __debug__:
        p.error('assertions must be enabled')
    a.cint, a.out = a.cint.resolve(), a.out.resolve()
    if a.out == ROOT or ROOT in a.out.parents:
        p.error('outputs must be outside the checkout')
    a.out.mkdir(parents=True, exist_ok=False)
    records = []

    def call(argv):
        t0 = time.monotonic_ns()
        r = subprocess.run(list(map(str, argv)), capture_output=True, timeout=3600)
        records.append({'argv': list(map(str, argv)), 'exit': r.returncode,
                        'ms': (time.monotonic_ns() - t0) // 1000000, 'stderr': r.stderr.decode(errors='replace')})
        if r.returncode != 0:
            raise AssertionError(str(argv) + '\n' + r.stderr.decode(errors='replace'))
        return r

    def emit(cint, kind, toolchain, dest):
        call([cint, 'emit-' + kind, '--root', ROOT / 'compiler', '--toolchain', toolchain, '--out', dest, 'main.ci'])
        return dest.parent / (dest / 'MANIFEST.ref').read_text().split()[0]

    result = {}
    try:
        c_tc = a.cint.parent / 'cint.toolchain'
        q_tc = cint_qbe_check.toolchain(a.cint, a.out, a.qbe)
        qk, ck = keys(q_tc), keys(c_tc)
        # 1. The reference emission through cpu-qbe, then QBE, as and ld.
        built = emit(a.cint, 'qbe', q_tc, a.out / 'built')
        objs = []
        for ssa in sorted(built.glob('*.ssa')):
            s, o = a.out / 'asm' / (ssa.stem + '.s'), a.out / 'obj' / (ssa.stem + '.o')
            s.parent.mkdir(exist_ok=True)
            o.parent.mkdir(exist_ok=True)
            call([qk['qbe'][0], '-t', qk['target'][0], '-o', s, ssa])
            call([qk['assembler'][0]] + qk.get('as_flag', []) + ['-o', o, s])
            objs.append(o)
        rel = Path(ck['runtime_object'][0]).parent
        build = rel.parent
        cli = sorted((build / 'cli').glob('*.o'))
        assert cli and (rel / 'cint_bridge.o').is_file() and (rel / 'cint_build.o').is_file()
        second = a.out / 'cint'
        call([qk['linker'][0]] + qk['link_before'] + ['-o', second] + cli + objs +
             [rel / 'cint_rt.o', rel / 'cint_bridge.o', rel / 'cint_build.o'] + qk['link_after'])
        # The second compiler's toolchain files: the reference ones for its own executable.
        digest = hashlib.sha256(second.read_bytes()).hexdigest()
        tcs = {}
        for name, src in (('c', c_tc), ('qbe', q_tc)):
            lines = [s for s in src.read_text().splitlines() if not s.startswith('executable_sha256 ')]
            tcs[name] = a.out / ('second-' + name + '.toolchain')
            tcs[name].write_text('\n'.join(lines + ['executable_sha256 ' + digest]) + '\n')
        # 2. Each compiler emits main.ci both ways; the trees agree.
        for kind, ref_tc in (('c', c_tc), ('qbe', q_tc)):
            ref = tree(emit(a.cint, kind, ref_tc, a.out / ('ref-' + kind)))
            got = tree(emit(second, kind, tcs[kind], a.out / ('second-' + kind)))
            if kind == 'qbe':
                assert tree(built) == got, 'not a fixpoint'
            differ = sorted(k for k in set(ref) | set(got) if ref.get(k) != got.get(k))
            assert not differ, 'emit-%s differs: %s' % (kind, differ[:8])
            result[kind] = {'files': len(ref), 'manifest_sha256': hashlib.sha256(ref['MANIFEST']).hexdigest()}
        result['second_cint_sha256'] = digest
        result['modules'] = len(objs)
    finally:
        (a.out / 'observations.json').write_text(json.dumps({'result': result, 'commands': records}, indent=2) + '\n')
    ms = {(r['argv'][0], r['argv'][1]): r['ms'] for r in records if len(r['argv']) > 1}
    t = lambda c, k: ms[(str(c), 'emit-' + k)]
    print('PASS cpu-qbe Q4 (%s): %d modules; emit-c %d files, emit-qbe %d files equal the reference; fixpoint' %
          (qk['target'][0], result['modules'], result['c']['files'], result['qbe']['files']))
    print('wall ms, reference / QBE-built: emit-c %d / %d, emit-qbe %d / %d' %
          (t(a.cint, 'c'), t(second, 'c'), t(a.cint, 'qbe'), t(second, 'qbe')))


if __name__ == '__main__':
    main()
