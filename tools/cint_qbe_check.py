"""Local probes of the cpu-qbe pipeline (SPEC-09 BACK-18): build, run and emit through QBE,
as and ld. Conformance is tools/cint_check.py --backend cpu-qbe; this checks the toolchain path.

python tools/cint_qbe_check.py --cint /path/to/cint --out /external/directory [--qbe PATH]
An explicit QBE/as/ld toolchain is retained; no C compiler handles program output.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'ref'))
from cint_ref.exec import run_program

# Verified QBE builds, by binary sha256: (source identity, what it is). The source identity is
# the upstream git commit (40 hex) or the sha256 of the release source tarball (64 hex).
QBE_PINS = {
    'ef7fb30f0104e05d51a80da1557373e409c78cc34b655c1b25627962133d7db9':
        ('c0818978acec60ebb6167fade60fb7012cbf20ca', 'QBE 1.3, arm64_apple build'),
    '2ef7f22c5e0ca6d9f9fdff9b1c5cb6ac8d10dcdfdbd4f7e55102dc51cd75e650':
        ('c0818978acec60ebb6167fade60fb7012cbf20ca', 'QBE 1.3, amd64_sysv build'),
    '6691f76ae3141e86858ecf5f7d7e113fc026a478d2565be0bec8940a80b1f569':
        ('c0818978acec60ebb6167fade60fb7012cbf20ca',
         'QBE 1.3 from qbe-1.3.tar.xz, musl-gcc -O2 -static -s, amd64_sysv build'),
    'cccdfe3dcf37e7d1120883d160a98d3ea4bc48d21a489f3db51f2c7f1d93281f':
        ('a6d50eb952525a234bf76ba151861f73b7a382ac952d985f2b9af1df5368225d',
         'QBE 1.2 from qbe_1.2.orig.tar.xz (Ubuntu pool), cc -std=c99 -g, amd64_sysv build'),
}


def crt(name):
    """A C runtime start file, located by the installed gcc; gcc compiles nothing here."""
    r = subprocess.run(['gcc', '-print-file-name=' + name], capture_output=True, text=True, check=True)
    path = Path(r.stdout.strip())
    if not path.is_absolute() or not path.is_file():
        raise ValueError('cannot locate ' + name)
    return str(path.resolve())


def darwin_arm64():
    """Apple arm64 keys: the system as and ld against the SDK's libSystem; no start files."""
    if os.uname().machine != 'arm64':
        raise ValueError('the Darwin leg is arm64 only')
    sdk = subprocess.run(['xcrun', '--show-sdk-path'], capture_output=True, text=True,
                         check=True).stdout.strip()
    sdk_version = json.loads((Path(sdk) / 'SDKSettings.json').read_text())['Version']
    major = subprocess.run(['sw_vers', '-productVersion'], capture_output=True, text=True,
                           check=True).stdout.strip().split('.')[0]
    return ['target arm64_apple', 'assembler /usr/bin/as', 'as_flag -arch', 'as_flag arm64',
            'linker /usr/bin/ld', 'link_before -arch', 'link_before arm64',
            'link_before -platform_version', 'link_before macos', 'link_before ' + major + '.0',
            'link_before ' + sdk_version, 'link_before -syslibroot', 'link_before ' + sdk,
            'link_after -lSystem']


def toolchain(cint, out, qbe=None):
    """Extend a retained bootstrap toolchain; every native command is explicit."""
    text = (cint.parent / 'cint.toolchain').read_text()
    lines = [s for s in text.splitlines() if not s.startswith(('env ', 'cc '))]
    qbe = qbe or shutil.which('qbe')
    digest = hashlib.sha256(Path(qbe).read_bytes()).hexdigest() if qbe else None
    if digest not in QBE_PINS:
        raise ValueError('QBE at %s matches no verified pin' % qbe)
    lines += ['cc /nonexistent-c-driver', 'backend cpu-qbe', 'qbe ' + str(Path(qbe).resolve()),
              'qbe_commit ' + QBE_PINS[digest][0], 'qbe_sha256 ' + digest]
    target = out / 'cpu-qbe.toolchain'
    if sys.platform == 'darwin':
        target.write_text('\n'.join(lines + darwin_arm64()) + '\n')
        return target
    gcc_dir = str(Path(crt('crtbegin.o')).parent)
    lines += ['target amd64_sysv', 'assembler /usr/bin/as', 'as_flag --64',
              'linker /usr/bin/ld', 'link_before -L' + gcc_dir,
              'link_before --dynamic-linker=/lib64/ld-linux-x86-64.so.2',
              'link_before ' + crt('crt1.o'), 'link_before ' + crt('crti.o'),
              'link_before ' + crt('crtbegin.o'),
              'link_after -lc', 'link_after -ldl', 'link_after -lgcc',
              'link_after ' + crt('crtend.o'), 'link_after ' + crt('crtn.o')]
    target.write_text('\n'.join(lines) + '\n')
    return target


def main():
    p = argparse.ArgumentParser(description=__doc__)
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
    tc = toolchain(a.cint, a.out, a.qbe)
    root = ROOT / 'compiler/tests/qbe'
    records = []

    def call(argv, want=0):
        r = subprocess.run(list(map(str, argv)), capture_output=True, timeout=300)
        records.append({'argv': list(map(str, argv)), 'exit': r.returncode,
                        'stdout_hex': r.stdout.hex(), 'stderr': r.stderr.decode(errors='replace')})
        if r.returncode != want:
            raise AssertionError(str(argv) + '\n' + r.stderr.decode(errors='replace'))
        return r

    checks = []
    try:
        for name in ('hello', 'empty', 'bytes', 'call', 'locals', 'state', 'arrays', 'records', 'ivar',
                     'refuse_extent'):
            src = root / (name + '.ci')
            ref = run_program(src.read_bytes(), name + '.ci', root=str(root))
            assert ref.kind == 'value'
            build = a.out / name
            call([a.cint, 'build', '--root', root, '--toolchain', tc,
                  '--cache', a.out / ('cache-' + name), '--out', build, name + '.ci'])
            fuel = a.out / (name + '.fuel')
            got = call([build / name, '-', fuel])
            assert got.stdout == ref.stdout and not got.stderr
            raw = fuel.read_bytes()
            assert len(raw) == 40 and struct.unpack('<q', raw[-8:])[0] == ref.fuel
            run = call([a.cint, 'run', '--root', root, '--toolchain', tc,
                        '--cache', a.out / ('run-' + name), name + '.ci'])
            assert run.stdout == ref.stdout and not run.stderr
            emitted = []
            for k in range(2):
                dest = a.out / (name + '-emit-' + str(k))
                call([a.cint, 'emit-qbe', '--root', root, '--toolchain', tc,
                      '--out', dest, name + '.ci'])
                stage = dest.parent / (dest / 'MANIFEST.ref').read_text().split()[0]
                files = {str(f.relative_to(stage)): f.read_bytes() for f in stage.rglob('*') if f.is_file()}
                assert not any(key.endswith('.c') for key in files)
                assert all(b'\r' not in value for value in files.values())
                emitted.append(files)
            assert emitted[0] == emitted[1]
            checks.append({'case': name, 'stdout_sha256': hashlib.sha256(ref.stdout).hexdigest(),
                           'fuel': ref.fuel, 'deterministic': True})
        r = call([a.cint, 'emit-qbe', '--root', root, '--toolchain', tc,
                  '--out', a.out / 'refuse_kernel', 'refuse_kernel.ci'], 2)
        assert b'E_UNSUPPORTED cpu-qbe capability entry' in r.stderr
        assert not (a.out / 'refuse_kernel' / 'MANIFEST.ref').exists()
        checks.append({'case': 'refuse_kernel', 'refused': True})
    finally:
        (a.out / 'observations.json').write_text(json.dumps({'checks': checks, 'commands': records}, indent=2) + '\n')
    print('PASS cpu-qbe local:', len(checks), 'probes; conformance is cint_check --backend cpu-qbe')


if __name__ == '__main__':
    main()
