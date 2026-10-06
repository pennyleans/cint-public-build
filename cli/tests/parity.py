"""Scoped B1 parity evidence from frozen fixtures and actual CLI outputs."""
from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'ref'))
sys.path.insert(0, str(ROOT / 'conformance/tools'))
from cint_ref import expect
from gen_expect import header

BOOT = ('boot/block_comment', 'boot/compound_assignment', 'boot/literal_octal_binary')
BOUNDARIES = (
    'control/else_if_126', 'control/else_if_1000', 'control/else_if_1000_minimal',
    'switch/arms_1000', 'switch/arms_1000_minimal',
    'control/nest_blocks_199', 'control/nest_blocks_200', 'control/nest_blocks_256',
    'control/nest_blocks_257', 'control/nest_whiles_66', 'control/nest_whiles_67',
    'control/nest_whiles_256', 'control/nest_for_49', 'control/nest_for_c_49',
    'control/nest_for_256',
)
BRACE_CASES = frozenset(BOUNDARIES[:5] + ('control/nest_blocks_256',))
WAITING = ('boot/string_literal_view', 'module/import_plain')
SEED_ONLY = tuple('boot/refuse_' + name for name in (
    'attribute', 'byte_string', 'defer', 'destructuring', 'enum', 'error_union',
    'fixed_point', 'fraction_literal', 'kernel', 'print', 'profile_line',
    'static_assert', 'ternary_literal', 'test_block', 'type_alias', 'where',
))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def c_brace_depth(data):
    """Count C braces after line splicing, ignoring comments and quoted tokens."""
    text = re.sub(rb'\\\r?\n', b'', data)
    at = depth = maximum = 0
    while at < len(text):
        if text[at:at + 2] == b'//':
            end = text.find(b'\n', at + 2)
            at = len(text) if end < 0 else end + 1
        elif text[at:at + 2] == b'/*':
            end = text.find(b'*/', at + 2)
            if end < 0:
                raise ValueError('unterminated C comment')
            at = end + 2
        elif text[at] in (34, 39):
            quote = text[at]
            at += 1
            while at < len(text) and text[at] != quote:
                at += 2 if text[at] == 92 else 1
            if at >= len(text):
                raise ValueError('unterminated C quoted token')
            at += 1
        else:
            if text[at] == 123:
                depth += 1
                maximum = max(maximum, depth)
            elif text[at] == 125:
                depth -= 1
                if depth < 0:
                    raise ValueError('unbalanced C braces')
            at += 1
    if depth:
        raise ValueError('unbalanced C braces')
    return maximum


def reference_observation(result, stdout):
    if result.returncode != 0 or result.stderr:
        raise ValueError('reference process failed: status %s, stderr %r' %
                         (result.returncode, result.stderr))
    parsed = expect.parse(result.stdout.decode('ascii'))
    kind = parsed.get('outcome')
    statuses = {'value': 0, 'fault': 1, 'compile-error': 2}
    if kind not in statuses:
        raise ValueError('reference cannot execute this case: ' + kind)
    if kind in ('value', 'fault'):
        if parsed.get('stdout-bytes') != str(len(stdout)):
            raise ValueError('reference stdout length differs from rendered outcome')
        digest = parsed.get('stdout-sha256')
        if digest != (sha(stdout) if stdout else None):
            raise ValueError('reference stdout digest differs from rendered outcome')
        if not re.fullmatch(r'[0-9]+', parsed.get('fuel-consumed', '')):
            raise ValueError('reference fuel is missing or invalid')
    elif stdout:
        raise ValueError('compile error produced stdout')
    return {'outcome': kind, 'exit_status': statuses[kind], 'fields': dict(parsed.lines),
            'stdout_bytes': len(stdout), 'stdout_sha256': sha(stdout)}


def frozen_cases(case):
    source = ROOT / 'conformance' / (case + '.ci')
    data = source.read_bytes()
    h = header(data.decode('ascii'))
    if h['args']:
        raise ValueError('parity fixture requires arguments: ' + case)
    rows = []
    for entry in h['entries']:
        suffix = '.' + entry if len(h['entries']) > 1 else ''
        path = source.with_name(source.stem + suffix + '.expect')
        frozen = path.read_bytes()
        rows.append({'case': case, 'entry': entry, 'header': h,
                     'source': str(source), 'source_sha256': sha(data),
                     'expectation': str(path), 'expectation_sha256': sha(frozen),
                     'expected': dict(expect.parse(frozen.decode('ascii')).lines)})
    return rows


def inventory():
    def rows(names, status):
        return [dict(row, status=status) for name in names for row in frozen_cases(name)]
    return {'boot': rows(BOOT, 'required'), 'boundary': rows(BOUNDARIES, 'required'),
            'waiting': rows(WAITING, 'waiting for task 2.14: views/structs/arrays/imports'),
            'excluded': rows(SEED_ONLY, 'seed-only: outside this B1 run')}


def verify_source_archive(path):
    data = path.read_bytes()
    if data[:8] != b'CISRC001':
        raise ValueError('invalid compiler source archive')
    count, = struct.unpack_from('<I', data, 8)
    at = 12
    for _ in range(count):
        size, = struct.unpack_from('<H', data, at)
        at += 2
        relative = data[at:at + size].decode('utf-8')
        at += size
        length, = struct.unpack_from('<Q', data, at)
        at += 8
        digest = data[at:at + 32]
        at += 32
        live = (ROOT / relative).read_bytes()
        if len(live) != length or hashlib.sha256(live).digest() != digest:
            raise ValueError('bootstrap compiler source differs: ' + relative)
    if at != len(data):
        raise ValueError('compiler source archive has trailing bytes')
    return sha(data)


def generated_modules(record):
    """Resolve only the output set declared by this invocation's receipt."""
    generated = record['identity']['generated']
    if generated is None:
        return {}
    cache = pathlib.Path(record['observations']['cache'])
    relative, digest = (cache / 'out/MANIFEST.ref').read_text().split()
    stage = cache / relative
    manifest = (stage / 'MANIFEST').read_bytes()
    if sha(manifest) != digest or digest != generated['manifest']:
        raise ValueError('generated manifest digest mismatch')
    declared = {}
    for line in manifest.decode('ascii').splitlines()[1:]:
        digest, _, name = line.split(' ', 2)
        declared[name] = digest
    if declared != generated['files']:
        raise ValueError('receipt and generated manifest file sets differ')
    files = {}
    for name, digest in declared.items():
        data = (stage / name).read_bytes()
        if sha(data) != digest:
            raise ValueError('generated file digest mismatch: ' + name)
        if name.endswith(('.c', '.sir', '.sites')) and not name.startswith('cint-program.'):
            files[name] = {'path': str(stage / name), 'sha256': digest}
            if name.endswith('.c'):
                files[name]['brace_depth'] = c_brace_depth(data)
    return files
