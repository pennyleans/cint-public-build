"""Checks deep, flat record declarations and public Bool validation."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

import aggregate_values as av

HOST = r'''
#include <stdio.h>
#include <string.h>
#include "cint_rt.h"
extern const cint_module_info cm_4_main;
cint_status cx_4_main_4_echo(cint_ctx *, int64_t, cint_view, cint_view);
cint_status cx_4_main_4_make(cint_ctx *, int64_t, cint_view);
cint_status cx_4_main_13_make_x5Farray(cint_ctx *, int64_t, cint_view);
int main(void) {
    unsigned char input[WIDTH], output[WIDTH], before[WIDTH];
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_buffer_id bi = 0, bo = 0;
    cint_view iv, ov;
    uint32_t reason = 0;
    int64_t fuel = -1;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config; config.program = cm_4_main.program;
    if (cm_4_main.record_count != DEPTH + 1u) { return 1; }
    for (uint32_t j = 0; j <= DEPTH; j++) {
        const cint_record_layout *r = &cm_4_main.records[j];
        if (r->bytes != (DEPTH - j) * PAD + 1u || r->align != CINT_ELEM_ALIGN(r->bytes)) { return 2; }
        if (j < DEPTH && (r->fields[PAD].type.record_id != j + 1u || r->fields[PAD].offset != PAD)) { return 3; }
    }
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 4; }
    memset(input, 0, sizeof input); memset(output, 0xA5, sizeof output);
    memcpy(before, output, sizeof output);
    if (cint_buffer_register_bytes(ctx, input, sizeof input, CINT_VIEW_WRITE, &bi) != CINT_OK ||
        cint_buffer_register_bytes(ctx, output, sizeof output, CINT_VIEW_WRITE, &bo) != CINT_OK) { return 5; }
    memset(&iv, 0, sizeof iv);
    iv.buffer = bi; iv.generation = CINT_BUFFER_GENERATION_FIRST;
    iv.type.code = CINT_TAG_RECORD; iv.type.record_id = 0;
    iv.rank = 1; iv.perm = CINT_VIEW_READ; iv.shape[0] = 1; iv.stride[0] = 1;
    ov = iv; ov.buffer = bo; ov.perm = CINT_VIEW_WRITE;
    input[WIDTH - 1] = 2;
    if (cx_4_main_4_echo(ctx, -1, iv, ov) != CINT_REFUSED ||
        cint_ctx_refusal(ctx, &reason) != CINT_OK || reason != CINT_REFUSAL_BOOL ||
        cint_fuel_consumed(ctx, &fuel) != CINT_OK || fuel != 0 ||
        memcmp(output, before, sizeof output) != 0) { return 6; }
    input[WIDTH - 1] = 1;
    if (cx_4_main_4_echo(ctx, -1, iv, ov) != CINT_OK || memcmp(input, output, sizeof input) != 0) { return 7; }
    memset(before, 0, sizeof before);
    if (cx_4_main_4_make(ctx, -1, ov) != CINT_OK || memcmp(output, before, sizeof output) != 0) { return 8; }
    memset(output, 0xA5, sizeof output);
    if (cx_4_main_13_make_x5Farray(ctx, -1, ov) != CINT_OK || memcmp(output, before, sizeof output) != 0) { return 9; }
    cint_ctx_destroy(ctx);
    puts("deep public: pass");
    return 0;
}
'''


def command(argv, cwd, env=None):
    r = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, capture_output=True, text=True)
    return dict(command=list(map(str, argv)), cwd=str(cwd), exit=r.returncode, stdout=r.stdout, stderr=r.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--array-fields', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    tc = av.RecordedToolchain('msvc')
    rt = tc.compile([av.ROOT / 'rt/cint_rt.c'], args.out, 'depth-runtime')
    rows = []
    for depth, pad in ([(33, 0), (257, 0)] if args.array_fields else [(256, 0), (257, 0), (257, 1)]):
        name = 'chain_%d_pad%d' % (depth, pad)
        directory = args.out / name
        directory.mkdir()
        source = ''.join('struct R%d { %sR%d%s child; }\n' %
                         (j, 'U8 marker; ' if pad else '', j+1, '[1]' if args.array_fields else '')
                         for j in range(depth))
        source += 'struct R%d { Bool bit; }\nexport R0 echo(R0 value) { return value; }\nexport R0 make() { R0 a; return a; }\nexport R0 make_array() { R0[1] a; return a[0]; }\nexport I64 run() { return 0; }\n' % depth
        path = directory / 'main.ci'
        path.write_bytes(source.encode())
        row = dict(case=name, source_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        row['reference'] = command([sys.executable, '-B', '-m', 'cint_ref', 'run', path, '--entry', 'run', '--path', 'main.ci', '--format', '2'], av.ROOT / 'ref')
        row['emit'] = command([args.exe, '--json', 'emit-c', '--root', directory, '--out', directory / 'out', 'main.ci'], av.ROOT)
        row['passed'] = False
        if row['emit']['exit'] == 0:
            sources = []
            for rel, data in av.golden_suite.output_set(directory / 'out').items():
                if rel.endswith('.c'):
                    target = directory / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    sources.append(target)
            host = directory / 'host.c'
            host.write_text('#define DEPTH %du\n#define PAD %du\n#define WIDTH %d\n' % (depth, pad, depth * pad + 1) + HOST, encoding='utf-8', newline='\n')
            row['host_sha256'] = hashlib.sha256(host.read_bytes()).hexdigest()
            try:
                objs = tc.compile(sources + [host], directory, 'depth-case')
                exe = tc.link_exe(objs + rt, directory / 'host', 'depth-case')
                row['host'] = command([exe], av.ROOT, tc.env)
                row['passed'] = (row['reference']['exit'] == 0 and 'return I64 0\n' in row['reference']['stdout']
                                 and row['host']['exit'] == 0 and not row['host']['stderr'])
            except av.cc.BuildError as error:
                row['build_error'] = str(error)
        rows.append(row)
        (args.out / 'results.json').write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8', newline='\n')
        print(name + ': ' + ('pass' if row['passed'] else 'FAIL'), flush=True)
    (args.out / 'native-commands.json').write_text(json.dumps(tc.commands, indent=2)+'\n', encoding='utf-8', newline='\n')
    passed = sum(row['passed'] for row in rows)
    print('aggregate depth: %d of %d pass' % (passed, len(rows)))
    return 0 if passed == len(rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
