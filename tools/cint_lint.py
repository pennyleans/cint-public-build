"""Lints the CINT trees: float tokens, CR bytes, and the SPEC-09 4.2 size targets.

This is a token lint, not a proof. It refuses binary floating-point types and
literals in the C sources of rt/, seed/, harness/, cli/, interp/ and workbench/ (tests
included) and in the Python of ref/cint_ref/ and python/cint/; it refuses any CR byte in
conformance/, compiler/, cli/, interp/, workbench/ and rt/*.h; it fails a component over its
SPEC-09 4.2 ceiling and reports one over its target. The CLI size rows count cli/*.c, with
cli/cint_receipt.c in its own row (decision 26); the CLI headers are reported separately. The
workbench row counts workbench/*.c and workbench/*.h (box 14 ruling BX14-03 A); workbench/tests/
is not counted.
Python test drivers under rt/, seed/, harness/, cli/, interp/ and workbench/, and python/tests/, are
host tooling and are not scanned. The one exemption is python/cint/_floats.py, the float
boundary of SPEC-03 section 8 (BX10-18): it may name the floating-point types, to read
binary64 and binary32 values as bit patterns and to refuse float arguments (P-23).
Float literals and true division stay refused there too.
"""
import argparse
import io
from pathlib import Path
import re
import sys
import tokenize

ROOT = Path(__file__).resolve().parents[1]

C_TREES = ("rt", "seed", "harness", "cli", "interp", "workbench")
PY_TREES = ("ref/cint_ref", "python/cint")
PY_NAMES_EXEMPT = ("python/cint/_floats.py",)   # SPEC-03 section 8 host boundary (BX10-18)
CR_TREES = ("conformance", "compiler", "cli", "interp", "workbench")  # compiler/: SPEC-09 CINTC-07
CR_GLOBS = ("rt/*.h",)
SKIP_DIRS = {"__pycache__", ".git"}

# The words are assembled so that this file does not match a grep for them.
C_FLOAT_WORDS = ("flo" + "at", "dou" + "ble", "_Float16", "_Float32", "_Float64", "_Float128",
                 "_Float32x", "_Float64x", "__float128", "__float80", "__fp16", "_Complex",
                 "_Imaginary")
C_FLOAT_HEADERS = ("float.h", "math.h", "tgmath.h", "complex.h", "fenv.h")
C_STRIP = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
C_WORD = re.compile(r"\b(" + "|".join(re.escape(w) for w in C_FLOAT_WORDS) + r")\b")
C_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.M)
# Decimal floating literals (1.0, .5, 1., 1e3) and hexadecimal ones (0x1p3).
C_FLOAT_LIT = re.compile(r"(?<![\w.])(?:\d+\.\d*|\.\d+|\d+(?=[eE][+-]?\d))(?:[eE][+-]?\d+)?[fFlL]?(?![\w.])"
                         r"|\b0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*[pP][+-]?\d+[fFlL]?\b")

PY_FLOAT_NAMES = {"flo" + "at", "complex"}
PY_FLOAT_MODULES = {"cmath", "statistics", "decimal"}
# `math` is admitted only through its integer-only functions.
PY_MATH_INTEGER = {"isqrt", "gcd", "lcm", "comb", "perm", "factorial"}
PY_FLOAT_OPS = {"/", "/="}

# SPEC-09 4.2: (name, files relative to ROOT, target, ceiling) in physical lines. A pattern
# that starts with "!" removes the files it matches from the row.
SIZE_ROWS = (
    ("seed (seed/*.c, seed/*.h)", ("seed/*.c", "seed/*.h"), 6000, 8000),
    ("cint_rt.h", ("rt/cint_rt.h",), 1500, 2500),
    ("cint_rt library (cint_rt.c, cint_rt_internal.h)", ("rt/cint_rt.c", "rt/cint_rt_internal.h"), 2500, 4000),
    ("cint_bridge.c (cint_bridge.c, cint_bridge_internal.h)", ("rt/cint_bridge.c", "rt/cint_bridge_internal.h"), 400, 600),
    ("cint_build.c", ("rt/cint_build.c",), 400, 600),
    ("cint_cuda.c", ("rt/cint_cuda.c",), 800, 1200),   # box 11 default BX11-08
    ("cint_cuda_dispatch.c", ("rt/cint_cuda_dispatch.c",), 500, 750),   # box 11 unit 4, Proposed
    ("cint_state.c", ("rt/cint_state.c",), 1000, 1500),  # box 13 default BX13-16
    # Decision 2026-10-06 on OQ-212: arenas, pools and handles, outside the cint_rt row.
    ("cint_mem (rt/cint_mem.c, rt/cint_mem.h)", ("rt/cint_mem.c", "rt/cint_mem.h"), 600, 900),
    ("cint-harness (harness/*.c, harness/*.h)", ("harness/*.c", "harness/*.h"), 600, 1000),
    ("cint CLI (cli/*.c except cli/cint_receipt.c)", ("cli/*.c", "!cli/cint_receipt.c"), 1500, 2500),
    ("cint CLI receipts (cli/cint_receipt.c)", ("cli/cint_receipt.c",), 400, 600),
    # Box 13 default BX13-16: an estimate, checked once the interpreter runs the suite.
    ("cint-interp (interp/*.c, interp/*.h)", ("interp/*.c", "interp/*.h"), 3000, 4500),
    ("cint workbench (workbench/*.c, workbench/*.h)", ("workbench/*.c", "workbench/*.h"), 8000, 12000),
)


def walk(root, rel, suffixes=None):
    base = root / rel
    if not base.is_dir():
        return []
    out = []
    for path in sorted(base.rglob("*")):
        if any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            continue
        if path.is_file() and path.suffix != ".pyc" and (suffixes is None or path.suffix in suffixes):
            out.append(path)
    return out


def line_of(text, index):
    return text.count("\n", 0, index) + 1


def c_findings(path, rel):
    raw = path.read_bytes().decode("utf-8", errors="replace")
    found = []
    for m in C_INCLUDE.finditer(raw):
        if Path(m.group(1)).name in C_FLOAT_HEADERS:
            found.append(f"{rel}:{line_of(raw, m.start())}: floating-point header <{m.group(1)}>")
    # Comments and literals become same-length blanks, so line numbers stay exact.
    code = C_STRIP.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), raw)
    for m in C_WORD.finditer(code):
        found.append(f"{rel}:{line_of(code, m.start())}: floating-point type `{m.group()}`")
    for m in C_FLOAT_LIT.finditer(code):
        found.append(f"{rel}:{line_of(code, m.start())}: floating-point literal `{m.group()}`")
    return found


def py_findings(path, rel, names_exempt=False):
    found = []
    source = path.read_bytes().decode("utf-8")
    skip = (tokenize.NL, tokenize.NEWLINE, tokenize.COMMENT, tokenize.INDENT, tokenize.DEDENT)
    tokens = [t for t in tokenize.generate_tokens(io.StringIO(source).readline) if t.type not in skip]
    for i, tok in enumerate(tokens):
        where = f"{rel}:{tok.start[0]}"
        previous = tokens[i - 1].string if i else None
        after = [t.string for t in tokens[i + 1:i + 3]]
        if tok.type == tokenize.NAME and previous != ".":
            if tok.string in PY_FLOAT_NAMES:
                if not names_exempt:
                    found.append(f"{where}: floating-point name `{tok.string}`")
            elif tok.string in PY_FLOAT_MODULES and previous in ("import", "from"):
                found.append(f"{where}: floating-point module `{tok.string}`")
            elif tok.string == "math" and previous == "from":
                found.append(f"{where}: write `import math` and `math.<integer function>`")
            elif tok.string == "math" and previous != "import" and (
                    len(after) < 2 or after[0] != "." or after[1] not in PY_MATH_INTEGER):
                found.append(f"{where}: `math` used other than through {sorted(PY_MATH_INTEGER)}")
        elif tok.type == tokenize.NUMBER and not re.fullmatch(r"0[xXoObB][0-9a-fA-F_]+|[0-9_]+", tok.string):
            found.append(f"{where}: floating-point literal `{tok.string}`")
        elif tok.type == tokenize.OP and tok.string in PY_FLOAT_OPS:
            found.append(f"{where}: true division `{tok.string}` yields a float; use // or divmod")
    return found


def float_findings(root):
    found, scanned = [], 0
    for tree in C_TREES:
        for path in walk(root, tree, {".c", ".h"}):
            scanned += 1
            found += c_findings(path, path.relative_to(root).as_posix())
    for tree in PY_TREES:
        for path in walk(root, tree, {".py"}):
            scanned += 1
            rel = path.relative_to(root).as_posix()
            found += py_findings(path, rel, rel in PY_NAMES_EXEMPT)
    return found, scanned


def cr_findings(root):
    paths = [p for tree in CR_TREES for p in walk(root, tree)]
    paths += [p for pattern in CR_GLOBS for p in sorted(root.glob(pattern)) if p.is_file()]
    found = []
    for path in paths:
        data = path.read_bytes()
        at = data.find(b"\r")
        if at >= 0:
            line = data.count(b"\n", 0, at) + 1
            found.append(f"{path.relative_to(root).as_posix()}:{line}: CR byte")
    return found, len(paths)


# Components a tree may leave out, as the public release does: a row whose directory is
# absent prints INFO. Any other component with no files fails.
OPTIONAL_COMPONENTS = ("interp", "workbench")


def size_rows(root):
    rows = []
    for name, patterns, target, ceiling in SIZE_ROWS:
        files = {p for pattern in patterns if not pattern.startswith("!") for p in root.glob(pattern) if p.is_file()}
        files = sorted(files - {p for pattern in patterns if pattern.startswith("!") for p in root.glob(pattern[1:])})
        lines = sum(p.read_bytes().count(b"\n") for p in files)
        status = "over-ceiling" if lines > ceiling else "over-target" if lines > target else "ok"
        top = patterns[0].split("/")[0]
        if not files and top in OPTIONAL_COMPONENTS and not (root / top).exists():
            status = "absent"
        rows.append((name, len(files), lines, target, ceiling, status))
    return rows


def run(root):
    """Print the findings; return 0 when nothing is refused, 1 otherwise."""
    floats, scanned = float_findings(root)
    crs, cr_scanned = cr_findings(root)
    sizes = size_rows(root)
    for line in floats + crs:
        print("FAIL " + line)
    failed = bool(floats or crs)
    for name, nfiles, lines, target, ceiling, status in sizes:
        tag = {"ok": "PASS", "over-target": "WARN", "over-ceiling": "FAIL", "absent": "INFO"}[status]
        failed |= status == "over-ceiling"
        if nfiles == 0 and status == "absent":
            tag = "INFO"
            status = "not in this tree"
        elif nfiles == 0:
            tag, failed = "FAIL", True
            status = "no files"
        print(f"{tag} size {name}: {lines:,} lines (target {target:,}, ceiling {ceiling:,}) {status}")
    cli_header_lines = sum(p.read_bytes().count(b"\n") for p in root.glob("cli/*.h") if p.is_file())
    print(f"INFO size cli headers (cli/*.h): {cli_header_lines:,} lines")
    verdict = "FAIL" if failed else "PASS"
    print(f"{verdict} cint lint: {scanned} sources scanned for float tokens ({len(floats)} found), "
          f"{cr_scanned} files scanned for CR ({len(crs)} found)")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: this checkout)")
    args = parser.parse_args()
    return run(args.root.resolve())


if __name__ == "__main__":
    sys.exit(main())
