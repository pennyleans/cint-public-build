"""Check the actual imported-name helpers, alias binding and deterministic work bounds.

Uses a retained seed, with every generated/compiled file under --out. Instrumentation
only increments counters in emitted test C; the production CINT files stay unchanged.
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import cint_check as cc  # noqa: E402


def instrument(text):
    names = {'import_x5Fbyte': 'a5_bytes', 'import_x5Finsert': 'a5_inserts'}
    prefix = 'static unsigned long a5_decisions, a5_bytes, a5_inserts, a5_rows;\n'
    for suffix, counter in names.items():
        pattern = r'(static bool ci_7_resolve_\d+_' + suffix + r'\([^;]+?\)\n\{\n)'
        text, hits = re.subn(pattern, r'\1    ++' + counter + ';\n', text)
        if hits != 1:
            raise AssertionError((suffix, hits))
    start = text.index('static bool ci_7_resolve_14_import_x5Ffind(', text.index('static bool ci_7_resolve_14_import_x5Ffind(') + 1)
    end = text.index('\nstatic bool ', start + 1)
    body, hits = re.subn(r'(    +int64_t v_bit = t\d+;)', r'    ++a5_decisions;\n\1', text[start:end])
    if hits != 1:
        raise AssertionError(('branch instrumentation', hits))
    text = text[:start] + body + text[end:]
    # Count each actual imported-block row visit. The loop reads kind once per row.
    start = text.index('static bool ci_7_resolve_14_import_x5Fbind(', text.index('static bool ci_7_resolve_14_import_x5Fbind(') + 1)
    end = text.index('\nstatic bool ', start + 1)
    body = text[start:end]
    body, hits = re.subn(r'(    +uint8_t v_kd = t\d+;)', r'    ++a5_rows;\n\1', body)
    if hits != 1:
        raise AssertionError(('block row instrumentation', hits))
    return prefix + text[:start] + body + text[end:]


REQUIRES = ["compiler/resolve.ci", "compiler/tests/import_names_host.c"]


def prepare(out):
    return None


def run(tc, tools, work, emitted, prepared, cc_module):
    generated = work / "import_resolve.c"
    rc, err = cc_module.run_seed(tools["seed"], ROOT / "compiler", "resolve.ci", generated)
    if rc:
        return 0, 1, [err]
    digest = hashlib.sha256(generated.read_bytes()).hexdigest()
    if emitted.setdefault("resolve.ci", digest) != digest:
        return 0, 1, ["seed output differs across configurations"]
    generated.write_text(instrument(generated.read_text()), newline="\n")
    host = work / "import_names_host.c"
    host.write_bytes((ROOT / "compiler/tests/import_names_host.c").read_bytes())
    objs = tc.compile([host], work, "import names")
    exe = tc.link_exe(objs + [tools["rt_obj"]], work / "import-names", "import names")
    result = subprocess.run([str(exe)], capture_output=True, env=tc.env)
    output = result.stdout.decode("utf-8")
    errors = result.stderr.decode("utf-8")
    if result.returncode or cc_module.sanitizer_reports(errors):
        return 0, 1, [output + errors]
    print(output.strip())
    return 1, 1, []


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--seed', type=pathlib.Path, required=True)
    ap.add_argument('--out', type=pathlib.Path, required=True)
    ap.add_argument('--leg', choices=('msvc', 'gcc', 'clang'), default='msvc')
    ap.add_argument('--opt', choices=('0', '2'), default='0')
    ap.add_argument('--helpers', choices=('portable', 'builtin'), default='portable')
    ap.add_argument('--sanitize', action='store_true')
    ns = ap.parse_args()
    out = ns.out.resolve()
    if out.is_relative_to(ROOT):
        ap.error('--out must be outside the source tree')
    if cc.available_memory() < cc.MIN_AVAILABLE_BYTES:
        print('BLOCKED: focused native test needs 8 GiB available')
        return 3
    out.mkdir(parents=True, exist_ok=True)
    generated = out / 'import_resolve.c'
    rc, err = cc.run_seed(ns.seed.resolve(), ROOT / 'compiler', 'resolve.ci', generated)
    if rc:
        print(err)
        return rc
    generated.write_text(instrument(generated.read_text()), newline='\n')
    host = out / 'import_names_host.c'
    host.write_bytes((ROOT / 'compiler/tests/import_names_host.c').read_bytes())
    tc = cc.Toolchain(ns.leg, ns.opt, ns.helpers, ns.sanitize)
    objs = tc.compile([host, ROOT / 'rt/cint_rt.c'], out, 'import names')
    exe = tc.link_exe(objs, out / 'import-names', 'import names')
    run = subprocess.run([str(exe)], capture_output=True, env=tc.env)
    sys.stdout.buffer.write(run.stdout)
    sys.stderr.buffer.write(run.stderr)
    return run.returncode


if __name__ == '__main__':
    raise SystemExit(main())
