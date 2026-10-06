"""The `golden` suite: compiler/main.ci with check.ci, lowering.ci, verify.ci and back_c.ci (tasks 2.10c, 2.11).

The compiler's exports take their tables as views, so this part builds
compiler/tests/golden_host.c with the C that cint-seed emits for compiler/main.ci and the
bridge (rt/cint_build.c, rt/cint_bridge.c), and builds programs exactly as `cint run`
will: cint_build reads the modules, calls plan, then compile, measure and emit per module,
and commits the output set (SPEC-09 CINTC-05, CINTC-06, CINTC-14; decision patch D-13).
Each item below counts once per configuration; an item is identical when what it compares
is equal byte for byte:

  - records: the record ids that compiler/main.ci puts in its table manifest are the ones
    the seed gives those structs (the cg_records table of the emitted C; CINTC-14, D-3);
  - golden: the SIR text (SIR-09) and the C (EMIT-16 to EMIT-24) of each program under
    compiler/tests/golden/ equal the reviewed files there: add.ci is the SPEC-09 6.3
    example, whose C has the shape of 7.4, and hello.ci is the M1 script;
  - two directories: every program built from two working directories, with different
    environment variables, gives the same output set (EMIT-21, EMIT-25); the goldens are
    the same files on every leg, and run.py prints the digest of each generated file;
  - brace depth: the function bodies in the C of control/nest_blocks_256 have brace
    depth at most 2 (D-5);
  - checked: a frozen compile-error case inside the checker's subset, built through
    main.ci, stops with the diagnostic code and position of its `.expect` file, so
    compile runs compiler/check.ci between parse and lower (CINTC-OQ-30); the C3004 case
    also carries the D-22 note position in detail[0] and detail[1] (CINTC-OQ-14);
  - refused: a module outside the checker's subset (switch.ci) stops with check.ci's C9102
    at the first construct the checker does not take, before lowering (CINTC-OQ-45);
  - digest: the `digest` export of main.ci (sha256.ci) equals hashlib's SHA-256 (M1);
  - revision: the revision identity in the program file (`cg_revision`) equals SHA-256 over
    the SIR-17 manifest assembled here from the program's SIR text output (`sir17_revision`,
    not the compiler's code); add.ci's equals the pinned ADD_REVISION, and add.ci with a
    blank line inserted before `export` has the same revision and a different site table
    (SPEC-09 SIR-17, SPEC-01 IM-157; compiler/OPEN.md CINTC-OQ-40); and the two modules of
    compiler/tests/golden/order/, built in the order z.ci, a.ci, have the revision of
    sir17_revision over both, which hashes a.ci first (decision 27, item 3: the
    revision is in byte order of path whatever the compile order);
  - runs: the generated C builds warning-free with the leg's EMIT-26 flags and runs. A
    module with exports is a shared library run through cint-harness, each call compared
    with cint_ref's `.expect` body (a frozen file for a conformance case); a script is an
    executable whose stdout bytes, exit status (SPEC-06 3.4a) and fault record (decoded
    with cint_ref's IM-149 reader) and fuel consumed (the fuel record of
    cint_program_run_fuel) are compared with cint_ref's outcome, and a fault
    record's revision with the program's SIR-17 revision (CONF-01 `fault.revision self`).
    The run of hello.ci is also compared with the 23 bytes of M1;
  - type signatures (SPEC-09 CONF-16, box 10 default BX10-29): the five examples of SPEC-03
    5.6 are golden bytes in compiler/tests/golden/type_signatures.txt, each line with the
    length and SHA-256 its bytes have; cint_ref writes each of them (advance from the 5.4
    kernel, both, paired, read_number and parse_u from their golden modules); cintc's
    reflection table rows of all but advance, a kernel, hold the same bytes; and in every program's output set the
    rows of the root module's exports equal cint_ref's type signatures, as
    tools/cint_check.py compares them for the conformance suite.

The expected values come from cint_ref and the frozen files, never from the compiler.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
GOLDEN = TESTS / "golden"
CONF = ROOT / "conformance"
REF = ROOT / "ref"
REQUIRES = ["compiler/main.ci", "compiler/check.ci", "compiler/resolve.ci", "compiler/lowering.ci",
            "compiler/verify.ci", "compiler/back_c.ci", "compiler/tests/golden_host.c", "compiler/tests/golden/add.ci",
            "compiler/tests/golden/hello.ci"]
sys.path.insert(0, str(REF))
sys.path.insert(0, str(CONF / "tools"))

M1_STDOUT = b"hello, world\nanswer=42\n"

# Programs: name, source root, module-relative path, and the golden files of its outputs
# (C, SIR text, program-level C), by the names in compiler/tests/golden/.
PROGRAMS = [
    ("add", GOLDEN, "add.ci", ("add.c", "add.sir", "add.program.c")),
    ("hello", GOLDEN, "hello.ci", ("hello.c", "hello.sir", "hello.program.c")),
    ("flow", GOLDEN, "flow.ci", None),
    ("switch", GOLDEN, "switch.ci", None),
    ("switch_checked", TESTS, "switch_checked.ci", None),
    ("prints", GOLDEN, "prints.ci", None),
    ("mainfn", GOLDEN, "mainfn.ci", None),
    ("arrays", GOLDEN, "arrays.ci", None),
    ("views", GOLDEN, "views.ci", None),
    ("public_views", GOLDEN, "public_views.ci", ("public_views.c", "public_views.sir", "public_views.program.c")),
    ("public_records", GOLDEN, "public_records.ci",
     ("public_records.c", "public_records.sir", "public_records.program.c")),
    ("structs", GOLDEN, "structs.ci", None),
    ("parse", GOLDEN, "parse.ci", None),
    ("nested_public", TESTS, "nested_public.ci", None),
    ("record_arrays", TESTS, "record_arrays.ci", None),
    ("named_constructors", TESTS, "named_constructors.ci", None),
    ("constructed_fields", TESTS, "constructed_fields.ci", None),
    ("zero_fields", TESTS, "zero_fields.ci", None),
    ("field_views", TESTS, "field_views.ci", None),
    ("array_constructors", TESTS, "array_constructors.ci", None),
    ("zero_records", TESTS, "zero_records.ci", None),
    ("switch_gate", TESTS, "switch_gate.ci", None),
    ("nest_blocks_256", CONF, "control/nest_blocks_256.ci", None),
    ("chain", TESTS, "chain.ci", None),
    ("print_no_newline_then_fault", CONF, "control/print_no_newline_then_fault.ci", None),
    ("print_hole_fault_partial", CONF, "control/print_hole_fault_partial.ci", None),
    ("print_holes", TESTS, "print_holes.ci", None),
    ("nest_cap", GOLDEN, "nest_cap.ci", ("nest_cap.c", "nest_cap.sir", "nest_cap.program.c")),
    ("nesting", TESTS, "nesting.ci", None),
]

DEPENDENCIES = {"public_records": ["record_lib.ci", "record_mid.ci"],
                "record_arrays": ["record_array_lib.ci"],
                "named_constructors": ["named_constructor_lib.ci"],
                "constructed_fields": ["named_constructor_lib.ci"],
                "zero_fields": ["zero_field_lib.ci"],
                "field_views": ["field_views_lib.ci"],
                "zero_records": ["zero_record_lib.ci"]}
PUBLIC_HOSTS = ("public_views", "public_records", "nested_public", "record_arrays", "zero_fields", "zero_records")

# Compile errors that check.ci reports through main.compile: a frozen case under
# conformance/, and the detail[0..1] it must carry (None when not compared).
CHECKED = [("diag/c2002_integer_condition.ci", None), ("diag/c2050_uninitialized_scalar.ci", None),
           ("diag/c3001_duplicate_name.ci", None), ("diag/c4001_missing_return.ci", None),
           ("arith/seed08_const_i64_overflow.ci", None), ("diag/c3004_script_selected_by_print.ci", (9, 1)),
           ("diag/c2001_implicit_widening.ci", None), ("diag/c2003_neg_paren_literal_i8.ci", None),
           ("diag/c2003_unsigned_minus_one.ci", None),
           ("diag/c2055_record_array_direct.ci", None), ("diag/c2055_record_array_indirect.ci", None),
           ("diag/c2055_zero_array_direct.ci", None), ("diag/c2055_zero_array_indirect.ci", None),
           ("view/record_array_readonly_element.ci", None), ("view/record_array_readonly_field.ci", None)]

# Modules outside the checker's subset, which compile refuses with check.ci's C9102 at the
# first construct the checker does not take (CINTC-OQ-15, CINTC-OQ-45): the module path
# under its source root and that construct's token, whose first token in
# cint_ref's lexer gives the expected position.
REFUSED = []

# A build whose compile order is not the byte order of its paths (decision 27, item
# 3): the source root and the module paths in the order the bridge compiles them.
ORDER = (GOLDEN / "order", ("z.ci", "a.ci"))

# The calls of switch.ci, run through the harness again once check.ci takes `switch` (task 2.13).
SWITCH_CALLS = [("switch", "sw", ["I64 %d" % v]) for v in (0, 1, 2, 4, 6, 7, -1, -9, -9223372036854775808)]
SWITCH_CALLS += [("switch", "sw_u8", ["U8 %d" % v]) for v in (97, 90, 95, 48, 57, 0, 31, 32, 255)]
SWITCH_CALLS += [("switch", "sw_bool", ["Bool %s" % v]) for v in ("true", "false")]

# Calls through the harness: program, function, typed arguments.
CALLS = [("add", "add3", a) for a in (["I64 1", "I64 2", "I64 3"], ["I64 -5", "I64 0", "I64 9"],
                                      ["I64 9223372036854775807", "I64 1", "I64 0"],
                                      ["I64 1", "I64 9223372036854775807", "I64 -9223372036854775807"])]
CALLS += [("flow", "chain", ["I64 %d" % v]) for v in (-5, 0, 3, 10, 19, 20)]
CALLS += [("flow", "loops", ["I64 %d" % v]) for v in (0, 1, 7, 60)]
CALLS += [("flow", "to_max", ["I64 %d" % v]) for v in (9223372036854775805, 9223372036854775807)]
CALLS += [("flow", "bytes", ["U8 %d" % a, "U8 %d" % b]) for a, b in ((200, 100), (0, 0), (255, 255))]
CALLS += [("flow", "logic", ["I64 %d" % a, "I64 %d" % b]) for a, b in ((3, 4), (3, 3), (-1, 5), (0, 0))]
CALLS += [("flow", "convert", ["I64 %d" % v]) for v in (70000, -1, 2147483647, 1431655765)]
CALLS += [("arrays", "squares", ["I64 %d" % v]) for v in (0, 3, 8, 9, -1)]   # task 2.13 part (b)
CALLS += [("arrays", "bytes", ["I64 %d" % v]) for v in (0, 1, 3, 4, -1)]
CALLS += [("arrays", "redeclared", ["I64 %d" % v]) for v in (0, 1, 3)]   # LS-67: zero each time
CALLS += [("views", "views", ["I64 %d" % a, "I64 %d" % i]) for a, i in   # task 2.13 part (ii)
          ((1, 0), (5, 3), (2, 4), (0, -1), (4611686018427387903, 0))]
CALLS += [("structs", "spans", ["I64 %d" % a, "I32 %d" % b]) for a, b in
          ((1, 2), (-5, 100), (4611686018427387904, 0), (0, 2147483637), (0, 2147483638))]
CALLS += [("flow", "shifts", ["I64 %d" % a, "I32 %d" % k]) for a, k in ((5, 3), (1, 62), (1, 63), (-1, 63),
                                                                       (3, 64), (3, -1))]
CALLS += [("flow", "calls", ["I64 %d" % v]) for v in (3, -4, 3037000499, 3037000500)]
CALLS += [("flow", "narrow", ["I64 %d" % v]) for v in (5, -128, 300, -129)]
CALLS += [("flow", "divide", ["I64 %d" % a, "I64 %d" % b]) for a, b in ((-7, 2), (7, -2), (1, 0),
                                                                       (-9223372036854775808, -1))]
CALLS += [("flow", "negate", ["I64 %d" % v]) for v in (5, -9223372036854775807, -9223372036854775808)]
CALLS += [("flow", "widths", ["U16 %d" % a, "I16 %d" % b]) for a, b in ((10, 5), (0, -32768), (65535, 32767))]
CALLS += [("flow", "literal_sources", ["I64 %d" % v]) for v in (1, 0)]
CALLS += [("nest_blocks_256", "run", [])]
CALLS += SWITCH_CALLS
CALLS += [("switch_checked", "constants", ["I64 %d" % v]) for v in (-5, 3, 7, 8, 40)]
CALLS += [("switch_checked", "nested", ["I64 %d" % v]) for v in (0, 1, 2, 3)]
CALLS += [("switch_checked", "u64_edges", ["U64 %d" % v])
          for v in (0, 1, (1 << 63) - 1, 1 << 63, (1 << 64) - 1)]
CALLS += [("switch_checked", "u64_cross", ["U64 %d" % v]) for v in (0, 1, (1 << 63) - 1, 1 << 63, (1 << 64) - 1)]
CALLS += [("switch_checked", "signed_edges", ["I64 %d" % v])
          for v in (-(1 << 63), -1, 0, 1, (1 << 63) - 1)]
for ty, bits in (("I8", 8), ("I16", 16), ("I32", 32), ("U8", 8), ("U16", 16), ("U32", 32)):
    values = (-(1 << (bits - 1)), -1, 0, (1 << (bits - 1)) - 1) if ty.startswith("I") else (0, 1, 2, (1 << bits) - 1)
    CALLS += [("switch_checked", ty.lower() + "_edges", ["%s %d" % (ty, v)]) for v in values]
CALLS += [("switch_checked", "literal_scrutinee", [])]
# Task 2.13 (a): operand chains of 1,000 terms, deeper than the parser's stack (chain.ci).
CALLS += [("chain", "sum_1000", ["I64 %d" % v]) for v in (0, 1, -7, 9223372036854775, 9223372036854776,
                                                         -9223372036854775, -9223372036854776)]
CALLS += [("chain", "all_1000", ["Bool %s" % b, "I64 %d" % v]) for b, v in (("true", 1), ("true", 0), ("false", 1))]
CALLS += [("chain", "wrap_1000", ["U8 %d" % v]) for v in (0, 1, 255)]
CALLS += [("record_arrays", name, [])
          for name in ("zeroed", "copied", "argument_read", "argument_order", "inout_element", "imported")]
CALLS += [("record_arrays", "assigned", ["I64 %d" % mode]) for mode in (0, 1, 2)]
CALLS += [("record_arrays", "nested_bounds", ["I64 %d" % outer, "I64 %d" % inner])
          for outer, inner in ((0, 0), (1, 1), (-1, 0), (2, 0), (0, -1), (0, 2))]
CALLS += [("record_arrays", "element_bounds", ["I64 %d" % index, "I64 %d" % divisor])
          for index, divisor in ((0, 2), (1, 1), (-1, 0), (2, 0), (0, 0))]
CALLS += [("record_arrays", "ordered", ["I64 %d" % outer, "I64 %d" % inner])
          for outer, inner in ((0, 1), (1, 0), (0, 2))]
CALLS += [("named_constructors", name, [])
          for name in ("single", "reordered", "mixed", "alias", "nested", "flag", "written_order")]
CALLS += [("named_constructors", "record_capture", ["I64 %d" % index]) for index in (0, 1, -1)]
CALLS += [("constructed_fields", name, [])
          for name in ("local", "imported", "nested", "flag", "copied", "written_order", "snapshot",
                       "unread_field_fault", "short_circuit")]
CALLS += [("constructed_fields", "early_bounds", ["I64 %d" % index]) for index in (0, 1, -1)]
CALLS += [("zero_fields", name, [])
          for name in ("copied", "positions", "imported", "repeated", "nested", "scalar_bounds", "record_bounds")]
CALLS += [("zero_fields", name, ["I64 %d" % index])
          for name in ("scalar_read", "record_read") for index in (-1, 0, 1)]
CALLS += [("field_views", name, [])
          for name in ("scalar", "nested", "mutable", "record", "empty", "imported", "capture", "once",
                       "empty_bounds", "shape_equal", "shape_different")]
CALLS += [("field_views", "outer_bounds", ["I64 %d" % index]) for index in (-1, 0, 2)]
CALLS += [("array_constructors", name, [])
          for name in ("scalar", "named", "records", "empty", "late_capture", "snapshot", "once", "fixed_view")]
CALLS += [("array_constructors", "indexed", ["I64 %d" % index]) for index in (-1, 0, 1, 2)]
CALLS += [("zero_records", name, [])
          for name in ("copied", "local_view", "nested_view", "indexed_view", "positive_copy", "imported",
                       "repeated", "positive_inout", "bounds_order", "inner_bounds", "nested_bounds")]
CALLS += [("zero_records", name, ["I64 %d" % index])
          for name in ("element_read", "element_write", "empty_read") for index in (-1, 0, 1, 2)]
CALLS += [("switch_gate", "run", ["I64 %d" % index]) for index in (0, 1)]
# OQ-205: a SIR golden nested past 16 levels (nest_cap.ci), and expressions nested 256
# levels (nesting.ci), which B1 refused with C9001 before the indentation cap.
CALLS += [("nest_cap", "run", ["Bool %s" % c, "I64 7"]) for c in ("true", "false")]
CALLS += [("nesting", fn, ["Bool %s" % c, "I64 7"]) for fn in ("cond_256", "then_256") for c in ("true", "false")]
CALLS += [("nesting", fn, ["Bool %s" % a, "Bool %s" % b]) for fn in ("or_256", "and_256")
          for a, b in (("true", "true"), ("true", "false"), ("false", "true"), ("false", "false"))]

# SPEC-03 5.6: the five examples of the canonical type signature (A-20 to A-22), the golden
# bytes of SPEC-09 CONF-16, and the programs whose cintc rows hold two of them.
SIGNATURES = GOLDEN / "type_signatures.txt"
SIGNATURE_NAMES = ("advance", "both", "read_number", "paired", "parse_u")
SIGNATURE_ROWS = (("both", "public_records"), ("read_number", "parse"), ("paired", "public_views"),
                  ("parse_u", "parse"))
ADVANCE_CI = b"""export kernel advance[n](in I64[n] pos, in I64[n] vel, in I64 dt, out I64[n] next) {
    next = pos + vel * dt;
}
"""

# Inputs of the digest export (plan section 5: checked against hashlib): files of
# compiler/tests/golden/, the empty message, one million `a` (FIPS 180-4), and the 3 MB C
# of main.ci.
DIGESTS = ["add.ci", "hello.ci", "empty", "million-a", "main.c"]

# The revision identity of add.ci (SIR-17's golden test): sir17_revision over the reviewed
# compiler/tests/golden/add.sir, runtime contract cint-rt-3.
ADD_REVISION = "fb64a16754f09ae94c9f6744b88de6791cf5a9dc4328e2c8a882ff36cef5c3e8"


def sir17_encoding(sir: bytes) -> bytes:
    """SIR-17's hashed encoding of a module's canonical SIR text (SIR-09): the `sites` line
    and every ` @<path>:<line>:<column>` after a site ordinal removed."""
    lines = sir.split(b"\n")
    if len(lines) < 2 or not lines[1].startswith(b"sites "):
        raise ValueError("the second line of SIR text is not its `sites` line")
    del lines[1]
    return b"\n".join(re.sub(rb"(#[0-9]+) @[^ ]+:[0-9]+:[0-9]+$", rb"\1", line) for line in lines)


def sir17_revision(modules) -> str:
    """SPEC-01 IM-157 over [(module-relative path, canonical SIR text)]: the domain
    string, the profile, the runtime contract version, the module count, then each module
    in byte order of path (U32 length and path, U64 length and encoded SIR)."""
    text = lambda b: len(b).to_bytes(4, "little") + b
    data = text(b"cint-core-1/revision/v1") + text(b"cint-core-1") + text(b"cint-rt-3")
    data += len(modules).to_bytes(4, "little")
    for path, sir in sorted(modules, key=lambda m: m[0].encode("utf-8")):
        enc = sir17_encoding(sir)
        data += text(path.encode("utf-8")) + len(enc).to_bytes(8, "little") + enc
    return hashlib.sha256(data).hexdigest()


def program_revision(program_c: bytes) -> str:
    """The 32 bytes of `cg_revision` in a program file, as hex, or '' when it is absent."""
    m = re.search(rb"static const uint8_t cg_revision\[32\] = \{\n(.*?)\n\};", program_c, re.S)
    if not m or not re.search(rb"cg_modules, cg_revision\};", program_c):
        return ""
    return bytes(int(h, 16) for h in re.findall(rb"0x([0-9a-f]{2})", m.group(1))).hex()


def fuel_line(path: pathlib.Path) -> str:
    """The fuel record a program wrote (rt/cint_rt.h CINT_FUEL_RECORD_DOMAIN) as the
    `.expect` line `fuel-consumed <n>`, or what is wrong with it."""
    domain = b"cint-core-1/fuel-consumed/v1"
    try:
        data = path.read_bytes()
    except OSError:
        return "no fuel record"
    head = len(domain).to_bytes(4, "little") + domain
    if len(data) != len(head) + 8 or not data.startswith(head):
        return "a malformed fuel record %r" % data[:64]
    return "fuel-consumed %d" % int.from_bytes(data[len(head):], "little", signed=True)


# Scripts run as programs: name, and the M1 bytes when the stdout is pinned.
SCRIPTS = [("hello", M1_STDOUT), ("prints", None), ("mainfn", None), ("print_no_newline_then_fault", None),
           ("print_hole_fault_partial", None), ("print_holes", None)]


def module_name(rel: str) -> str:
    return rel[:-3].replace("/", ".")


def program(name):
    return next(p for p in PROGRAMS if p[0] == name)


def ref_run(root: pathlib.Path, rel: str, gen: pathlib.Path, entry=None, args=()):
    """cint_ref's `.expect` lines from `outcome` on, and the program's stdout bytes."""
    out = gen / ("ref-%s.stdout" % hashlib.sha256(("%s %s %s" % (rel, entry, args)).encode()).hexdigest()[:16])
    cmd = [sys.executable, "-m", "cint_ref", "run", "--path", rel, "--stdout", str(out)]
    if entry:
        cmd += ["--entry", entry]
    for a in args:
        cmd += ["--arg", a]
    cmd.append(str(root / rel))
    r = subprocess.run(cmd, cwd=REF, capture_output=True, text=True, env=dict(os.environ, PYTHONPATH=str(REF)))
    if r.returncode != 0 and not r.stdout:
        raise SystemExit("cint_ref failed on %s: %s" % (rel, r.stderr))
    lines = r.stdout.rstrip("\n").split("\n")
    first = next(i for i, line in enumerate(lines) if line.startswith("outcome "))
    return lines[first:], out.read_bytes() if out.is_file() else b""


def first_token(path: pathlib.Path, rel: str, text: str) -> tuple:
    """(line, column) of the first token of the source at path that spells text (cint_ref's lexer)."""
    from cint_ref.lexer import decode_source, tokenize
    for t in tokenize(decode_source(path.read_bytes(), rel), rel):
        if t.text == text:
            return t.line, t.col
    raise SystemExit("no `%s` token in %s" % (text, rel))


def frozen_body(rel: str):
    lines = (CONF / rel).with_suffix(".expect").read_bytes().decode("ascii").rstrip("\n").split("\n")
    first = next(i for i, line in enumerate(lines) if line.startswith("outcome "))
    return lines[first:]


def golden_signatures() -> tuple:
    """compiler/tests/golden/type_signatures.txt as {export: bytes}, and its problems: a line
    not in the form `<export> <length> <sha256> <hex>`, a length or SHA-256 its bytes do not
    have, and exports other than the five of SPEC-03 5.6."""
    found, problems = {}, []
    for line in SIGNATURES.read_bytes().decode("ascii").split("\n")[:-1]:
        if line.startswith("#"):
            continue
        m = re.fullmatch(r"([a-z_]+) ([0-9]+) ([0-9a-f]{64}) ((?:[0-9a-f]{2})+)", line)
        if not m:
            problems.append("a line not in the form <export> <length> <sha256> <hex>: " + line[:80])
            continue
        data = bytes.fromhex(m.group(4))
        if len(data) != int(m.group(2)) or hashlib.sha256(data).hexdigest() != m.group(3):
            problems.append("%s: the bytes do not have the length and SHA-256 of the line" % m.group(1))
        found[m.group(1)] = data
    if sorted(found) != sorted(SIGNATURE_NAMES):
        problems.append("the exports are %s, not the five of SPEC-03 5.6" % ", ".join(sorted(found)))
    return found, problems


def ref_signatures() -> dict:
    """cint_ref's type signatures of the five examples of SPEC-03 5.6."""
    from cint_ref import sig

    def export(source: bytes, rel: str, root, name: str) -> bytes:
        return next(e.signature for e in sig.program_exports(source, rel, root) if e.name == name)

    def golden(rel: str, name: str) -> bytes:
        return export((GOLDEN / rel).read_bytes(), rel, str(GOLDEN), name)
    return {"advance": export(ADVANCE_CI, "advance.ci", None, "advance"),
            "both": golden("public_records.ci", "both"),
            "read_number": golden("parse.ci", "read_number"),
            "paired": golden("public_views.ci", "paired"),
            "parse_u": golden("parse.ci", "parse_u")}


def ref_exports() -> dict:
    """cint_ref's exports (ref/cint_ref/sig.py) of the root module of every program."""
    from cint_ref import sig
    from cint_ref.faults import CompileError, Refused
    exports = {}
    for name, root, rel, _ in PROGRAMS:
        try:
            exports[name] = sig.program_exports((root / rel).read_bytes(), rel, str(root))
        except (CompileError, Refused) as e:
            raise SystemExit("cint_ref has no type signatures for %s: %s" % (rel, e))
    return exports


def signature_items(sets: dict, prepared: dict, cc) -> list:
    """The type signature items (module docstring) over the output sets of the programs built."""
    items = []
    golden, problems = golden_signatures()
    items.append(("type signature vectors", "; ".join(problems)))
    for name in SIGNATURE_NAMES:
        have = prepared["signatures"][name]
        items.append(("type signature %s cint_ref" % name, "" if have == golden.get(name) else
                      "cint_ref writes %s" % have.hex()))
    for name, program_name in SIGNATURE_ROWS:
        rel = program(program_name)[2]
        if program_name not in sets:
            items.append(("type signature %s cintc" % name, "not built"))
            continue
        rows, problems = cc.table_rows(sets[program_name].get(rel[:-3] + ".c", b"").decode("utf-8", "replace"))
        have = dict(rows).get(name)
        items.append(("type signature %s cintc" % name, "; ".join(problems) or (
            "" if have == golden.get(name) else "no row" if have is None else "cintc writes %s" % have.hex())))
    for name, _, rel, _ in PROGRAMS:
        if name not in sets:
            items.append(("type signatures %s" % name, "not built"))
            continue
        _, bad = cc.signature_problems(sets[name], [*DEPENDENCIES.get(name, []), rel], rel,
                                       prepared["exports"][name])
        items.append(("type signatures %s" % name, "; ".join(json.dumps(d, sort_keys=True)[:300]
                                                             for d in bad[:3])))
    return items


def prepare(gen: pathlib.Path) -> dict:
    """The expected outcomes, computed once: cint_ref and the frozen files."""
    gen.mkdir(parents=True, exist_ok=True)
    calls = []
    for name, fn, args in CALLS:
        _, root, rel, _ = program(name)
        if root == CONF:
            want = frozen_body(rel)
        else:
            want, _ = ref_run(root, rel, gen, fn, args)
        calls.append((name, fn, args, want))
    scripts = []
    for name, pinned in SCRIPTS:
        _, root, rel, _ = program(name)
        want, stdout = ref_run(root, rel, gen)
        if root == CONF and want != frozen_body(rel):
            raise SystemExit("cint_ref differs from the frozen %s" % rel)
        if pinned is not None and stdout != pinned:
            raise SystemExit("cint_ref's stdout of %s is not the M1 bytes" % rel)
        scripts.append((name, want, stdout))
    return {"calls": calls, "scripts": scripts, "signatures": ref_signatures(), "exports": ref_exports()}


def brace_depth(text: bytes) -> int:
    """The largest brace depth of the function bodies of C text, braces in comments and
    literals excluded. A brace at file scope opens a function body when it follows `)`.
    D-5 bounds function bodies; the depth of a data initializer is fixed by its table and
    not counted (the rows of the reflection table, SPEC-03 A-19, nest a cint_name in a
    cint_export in an array, depth 3)."""
    depth = most = 0
    body = False
    last = b""
    i, n = 0, len(text)
    while i < n:
        c = text[i:i + 1]
        if text.startswith(b"/*", i):
            i = text.index(b"*/", i) + 2
            continue
        if c in (b'"', b"'"):
            j = i + 1
            while text[j:j + 1] != c:
                j += 2 if text[j:j + 1] == b"\\" else 1
            i = j + 1
            last = c
            continue
        if c == b"{":
            if depth == 0:
                body = last == b")"
            depth += 1
            if body:
                most = max(most, depth)
        elif c == b"}":
            depth -= 1
        if not c.isspace():
            last = c
        i += 1
    return most


def output_set(out: pathlib.Path) -> dict:
    """The committed output set under out: {relative path: bytes}, MANIFEST included."""
    ref = (out / "MANIFEST.ref").read_bytes().decode("ascii").split()
    stage = out.parent / ref[0]
    manifest = (stage / "MANIFEST").read_bytes()
    if hashlib.sha256(manifest).hexdigest() != ref[1]:
        raise ValueError("MANIFEST digest differs from MANIFEST.ref")
    files = {"MANIFEST": manifest}
    for line in manifest.decode("ascii").splitlines()[1:]:
        digest, _, rel = line.split(" ", 2)
        data = (stage / rel).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("%s differs from its MANIFEST digest" % rel)
        files[rel] = data
    return files


def records(c_text: str) -> dict:
    m = re.search(r"cg_records\[\d+\] = \{\n(.*?)\n\};", c_text, re.S)
    names = re.findall(r"sizeof\((ci_\w+)\)\)", m.group(1)) if m else []
    return {nm.rsplit("_", 1)[-1]: i for i, nm in enumerate(names)}


def check_records(c_text: str) -> str:
    """'' when the R_* ids of compiler/main.ci, and records 0 and 1, are the seed's."""
    seed = records(c_text)
    main = (ROOT / "compiler" / "main.ci").read_text(encoding="ascii")
    names = {"TOKEN": "Token", "NODE": "Node", "FRAME": "Frame", "PMOD": "PMod", "SIR": "Sir", "FUNC": "SirFunc",
             "SLOT": "SirSlot", "LSTATE": "LState", "SEM": "Sem", "SYM": "Sym", "NAME": "Name", "DECL": "Decl"}
    bad = []
    for key, value in re.findall(r"const U32 R_(\w+) = (\d+);", main):
        if seed.get(names[key]) != int(value):
            bad.append("R_%s %s, seed %s" % (key, value, seed.get(names[key])))
    if seed.get("TablePlan") != 0 or seed.get("CompilerDiag") != 1:
        bad.append("TablePlan and CompilerDiag are not records 0 and 1")
    return "; ".join(bad)


def run(tc, tools, work: pathlib.Path, emitted: dict, prepared: dict, cc) -> tuple:
    """Builds the host for configuration tc, builds every program, and compares."""
    items = []          # (label, problem or '')

    def item(label, problem=""):
        items.append((label, problem))

    total_planned = (1 + 2 * len(PUBLIC_HOSTS) + 2 * len(PROGRAMS) + 1 + 3 + len(CHECKED) + len(REFUSED) + len(CALLS) + len(SCRIPTS)
                     + len(DIGESTS) + 1 + len(SIGNATURE_NAMES) + len(SIGNATURE_ROWS) + len(PROGRAMS))
    c_file = work / "main.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "main.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, total_planned, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text or emitted.setdefault("main.ci", digest) != digest:
        return 0, total_planned, ["the emitted C of main.ci holds a CR byte or differs between configurations"]
    item("records", check_records(text.decode("ascii")))
    try:
        objs = tc.compile([c_file, TESTS / "golden_host.c", ROOT / "rt" / "cint_bridge.c",
                           ROOT / "rt" / "cint_build.c"], work, "golden")
        host = tc.link_exe(objs + [tools["rt_obj"]], work / "golden-host", "golden")
    except cc.BuildError as e:
        return 0, total_planned, ["build: %s" % " ".join(str(e).split())[:600]]
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    noisy = dict(env, CINT_GOLDEN_NOISE="1", TZ="Pacific/Kiritimati", LANG="tr_TR.UTF-8", LC_ALL="tr_TR.UTF-8")
    sets = {}
    revisions = {}
    for name, root, rel, golden in PROGRAMS:
        got = []
        for cwd, e in (("cwd-a", env), ("cwd-b", noisy)):
            d = work / name / cwd
            d.mkdir(parents=True, exist_ok=True)
            out = d / "out"
            r = subprocess.run([str(host), str(root), str(out), *DEPENDENCIES.get(name, []), rel],
                               cwd=d, env=e, capture_output=True)
            stdout = r.stdout.decode("ascii", "replace")
            if r.returncode != 0 or cc.sanitizer_reports(r.stderr.decode("utf-8", "replace")) or \
                    stdout.splitlines()[:1] != ["build 0"]:
                got.append("host exit %d: %s %s" % (r.returncode, " ".join(stdout.split())[:300],
                                                    " ".join(r.stderr.decode("utf-8", "replace").split())[:300]))
                continue
            try:
                got.append(output_set(out))
            except (OSError, ValueError, IndexError) as ex:
                got.append("output set: %s" % ex)
        if any(isinstance(g, str) for g in got):
            item("two directories %s" % name, "; ".join(g for g in got if isinstance(g, str)))
            if golden:
                for g in golden:
                    item("golden %s" % g, "not built")
            continue
        item("two directories %s" % name, "" if got[0] == got[1] else "the output sets differ")
        files = got[0]
        sets[name] = files
        stem = rel[:-3]
        for path, data in sorted(files.items()):
            if path != "MANIFEST":
                emitted.setdefault("golden:%s:%s" % (name, path), hashlib.sha256(data).hexdigest())
        if golden:
            for gfile, out_rel in zip(golden, (stem + ".c", stem + ".sites", "cint-program.c")):
                want = (GOLDEN / gfile).read_bytes()
                have = files.get(out_rel)
                item("golden %s" % gfile, "" if have == want else "%s differs from %s" % (out_rel, gfile))
        for path, data in files.items():
            if path.endswith(".c") and (b"\r" in data or b"\t" in data or b" \n" in data or not data.endswith(b"\n")):
                item("format %s" % path, "a CR, a tab, a trailing space, or no final LF (EMIT-24)")
        if name == "nest_blocks_256":
            depth = brace_depth(files["control/nest_blocks_256.c"])
            item("brace depth", "" if depth <= 2 else "brace depth %d" % depth)
        have = program_revision(files.get("cint-program.c", b""))
        try:
            want = sir17_revision([(path, files[path[:-3] + ".sites"])
                                   for path in [*DEPENDENCIES.get(name, []), rel]])
        except (KeyError, ValueError) as ex:
            want = "unassembled: %s" % ex
        revisions[name] = have
        item("revision %s" % name, "" if have == want else "cg_revision %r, SIR-17 manifest %s" % (have, want))
    for name in PUBLIC_HOSTS:
        if name not in sets:
            item(name + " host", "not built")
            continue
        d = work / name / "host"
        d.mkdir(parents=True, exist_ok=True)
        sources = []
        for path, data in sorted(sets[name].items()):
            if path.endswith(".c"):
                source = d / path
                source.write_bytes(data)
                sources.append(source)
        try:
            objects = tc.compile(sources, d, name)
            host_objects = tc.compile([TESTS / (name + "_host.c")], d, name)
            executable = tc.link_exe(objects + host_objects + [tools["rt_obj"]], d / name, name)
            result = subprocess.run([str(executable)], cwd=d, env=env, capture_output=True)
            item(name + " host", "" if result.returncode == 0 and not result.stderr else
                 (result.stdout + result.stderr).decode("utf-8", "replace"))
            library = tc.link_shared(objects + [tools["rt_obj"]], d / (name + "-audit"), name)
            symbols = [tc.symbols(obj) for obj in objects]
            exports = tc.exports(library)
            # EMIT-20: instrumented objects are exempt from the symbol audit.
            if not tc.sanitize:
                item(name + " symbol audit", "; ".join(cc.audit_program(symbols, exports,
                                                                        tc.symbols(tools["rt_obj"])[0], tc.leg)))
            if name == "public_records":
                public = {symbol for symbol in exports if symbol.startswith(("cx", "cm"))}
                expected = {"cm_17_public_x5Frecords", "cm_13_record_x5Flib", "cm_13_record_x5Fmid"}
                expected.update("cx_17_public_x5Frecords_" + suffix
                                for suffix in ("5_touch", "6_copied", "4_both", "7_aliases"))
                item("record public inventory", "" if public == expected else str(sorted(public ^ expected)))
                item("imported scalar observer", "" if b"cg_13_record_x5Flib_obs[1]" in
                     sets[name]["record_lib.c"] else "missing")
        except cc.BuildError as error:
            item(name + " host", str(error))
    for label, problem in signature_items(sets, prepared, cc):
        item(label, problem)
    # SIR-17's golden tests: the pinned revision of add.ci, and a blank line before `export`.
    item("revision golden add.ci", "" if revisions.get("add") == ADD_REVISION else
         "%r, want %s" % (revisions.get("add"), ADD_REVISION))
    d = work / "add_blank"
    d.mkdir(parents=True, exist_ok=True)
    (d / "src").mkdir(exist_ok=True)
    source = (GOLDEN / "add.ci").read_bytes()
    (d / "src" / "add.ci").write_bytes(source.replace(b"\nexport ", b"\n\nexport ", 1))
    r = subprocess.run([str(host), str(d / "src"), str(d / "out"), "add.ci"], cwd=d, env=env, capture_output=True)
    problem = ""
    try:
        blank = output_set(d / "out") if r.returncode == 0 else None
    except (OSError, ValueError, IndexError) as ex:
        blank, problem = None, "output set: %s" % ex
    if blank is None:
        problem = problem or "host exit %d" % r.returncode
    elif program_revision(blank.get("cint-program.c", b"")) != ADD_REVISION:
        problem = "revision %r, want %s" % (program_revision(blank.get("cint-program.c", b"")), ADD_REVISION)
    elif "add" in sets and blank.get("add.sites") == sets["add"].get("add.sites"):
        problem = "the site table did not change"
    item("revision blank line add.ci", problem)
    # Two modules compiled z.ci, then a.ci: the revision hashes a.ci first (SIR-17).
    d = work / "order"
    d.mkdir(parents=True, exist_ok=True)
    root, rels = ORDER
    r = subprocess.run([str(host), str(root), str(d / "out"), *rels], cwd=d, env=env, capture_output=True)
    problem = ""
    try:
        files = output_set(d / "out") if r.returncode == 0 and r.stdout.splitlines()[:1] == [b"build 0"] else None
    except (OSError, ValueError, IndexError) as ex:
        files, problem = None, "output set: %s" % ex
    if files is None:
        problem = problem or "host exit %d: %s" % (r.returncode, " ".join(r.stdout.decode("ascii", "replace").split()))
    else:
        try:
            want = sir17_revision([(rel, files[rel[:-3] + ".sites"]) for rel in rels])
        except (KeyError, ValueError) as ex:
            want = "unassembled: %s" % ex
        have = program_revision(files.get("cint-program.c", b""))
        if have != want:
            problem = "cg_revision %r, SIR-17 manifest in path order %s" % (have, want)
    item("revision order z.ci, a.ci", problem)
    # Compile errors from check.ci through main.compile, against the frozen `.expect` lines.
    for rel, detail in CHECKED:
        d = work / "checked" / rel[:-3].replace("/", "_")
        d.mkdir(parents=True, exist_ok=True)
        r = subprocess.run([str(host), str(CONF), str(d / "out"), rel], cwd=d, env=env, capture_output=True)
        lines = r.stdout.decode("ascii", "replace").splitlines()
        exp = {}
        for line in (CONF / (rel[:-3] + ".expect")).read_text(encoding="utf-8").splitlines():
            key, _, val = line.partition(" ")
            exp.setdefault(key, val)
        line_no, col = exp["diagnostic.position"].rsplit(":", 2)[1:]
        want = ["build 1", "diag %s 0 %s %s" % (exp["diagnostic.code"][1:], line_no, col)]
        got = lines[:1] + [" ".join(lines[1].split()[:5])] if len(lines) == 2 else lines
        problem = ""
        if r.returncode != 0 or cc.sanitizer_reports(r.stderr.decode("utf-8", "replace")) or got != want:
            problem = "got %s, want %s (exit %d)" % (lines, want, r.returncode)
        elif detail is not None and lines[1].split()[5:7] != [str(v) for v in detail]:
            problem = "detail %s, want %s" % (lines[1].split()[5:7], list(detail))
        item("checked %s" % rel, problem)
    # C9102 from check.ci through main.compile, which stops before lowering (CINTC-OQ-45).
    for root, rel, keyword in REFUSED:
        d = work / "refused" / rel[:-3].replace("/", "_")
        d.mkdir(parents=True, exist_ok=True)
        r = subprocess.run([str(host), str(root), str(d / "out"), rel], cwd=d, env=env, capture_output=True)
        lines = r.stdout.decode("ascii", "replace").splitlines()
        want = ["build 1", "diag 9102 0 %d %d" % first_token(root / rel, rel, keyword)]
        got = lines[:1] + [" ".join(lines[1].split()[:5])] if len(lines) == 2 else lines
        problem = ""
        if r.returncode != 0 or cc.sanitizer_reports(r.stderr.decode("utf-8", "replace")) or got != want:
            problem = "got %s, want %s (exit %d)" % (lines, want, r.returncode)
        item("refused %s" % rel, problem)
    # The digest export (sha256.ci through main.ci) against hashlib.
    inputs = []
    for label in DIGESTS:
        f = work / ("digest-" + label)
        if label == "empty":
            f.write_bytes(b"")
        elif label == "million-a":
            f.write_bytes(b"a" * 1000000)
        elif label == "main.c":
            f = c_file
        else:
            f = GOLDEN / label
        inputs.append(f)
    r = subprocess.run([str(host), "--digest"] + [str(f) for f in inputs], cwd=work, env=env, capture_output=True)
    lines = r.stdout.decode("ascii", "replace").splitlines()
    for k, (label, f) in enumerate(zip(DIGESTS, inputs)):
        want = "digest " + hashlib.sha256(f.read_bytes()).hexdigest()
        got = lines[k] if k < len(lines) else "no output (exit %d)" % r.returncode
        item("digest %s" % label, "" if got == want else "%s, want %s" % (got, want))
    # Runs: the harness programs, then the scripts.
    libs = {}
    for name in sorted({c[0] for c in prepared["calls"]}):
        if name not in sets:
            continue
        d = work / name / "build"
        d.mkdir(parents=True, exist_ok=True)
        srcs = []
        for path, data in sorted(sets[name].items()):
            if path.endswith(".c"):
                f = d / path.replace("/", "_")
                f.write_bytes(data)
                srcs.append(f)
        try:
            objs = tc.compile(srcs, d, name)
            libs[name] = tc.link_shared(objs + [tools["rt_obj"]], d / name, name)
        except cc.BuildError as e:
            libs[name] = "build: %s" % " ".join(str(e).split())[:600]
    for name in sorted(libs):
        calls = [c for c in prepared["calls"] if c[0] == name]
        lib = libs[name]
        if isinstance(lib, str):
            for _, fn, args, _ in calls:
                item("run %s %s %s" % (name, fn, " ".join(args)), lib)
            continue
        rel = program(name)[2]
        cases = work / name / "build" / "cases.txt"
        cases.write_bytes("".join(" ".join([module_name(rel), fn] + list(args)) + "\n"
                                  for _, fn, args, _ in calls).encode("ascii"))
        r = subprocess.run([str(tools["harness"]), str(lib), "--cases", str(cases), "--format", "1"],
                           cwd=work / name / "build", env=env, capture_output=True)
        stderr = r.stderr.decode("utf-8", "replace")
        blocks = cc.parse_harness(r.stdout, len(calls))
        for (_, fn, args, want), got in zip(calls, blocks):
            problem = ""
            if r.returncode != 0 or cc.sanitizer_reports(stderr):
                problem = "harness exit %d: %s" % (r.returncode, " ".join(stderr.split())[:300])
            elif got != want:
                problem = "want %s, got %s" % (want, got)
            item("run %s %s %s" % (name, fn, " ".join(args)), problem)
    from cint_ref import faults
    for name, want, stdout in prepared["scripts"]:
        label = "run script %s" % name
        if name not in sets:
            item(label, "not built")
            continue
        d = work / name / "exe"
        d.mkdir(parents=True, exist_ok=True)
        srcs = []
        for path, data in sorted(sets[name].items()):
            if path.endswith(".c"):
                f = d / path.replace("/", "_")
                f.write_bytes(data)
                srcs.append(f)
        try:
            objs = tc.compile(srcs, d, name)
            exe = tc.link_exe(objs + [tools["rt_obj"]], d / name, name)
        except cc.BuildError as e:
            item(label, "build: %s" % " ".join(str(e).split())[:600])
            continue
        record = d / "fault.bin"
        fuel = d / "fuel.bin"
        for f in (record, fuel):
            if f.exists():
                f.unlink()
        r = subprocess.run([str(exe), str(record), str(fuel)], cwd=d, env=env, capture_output=True)
        outcome = want[0].split(" ", 1)[1]
        status = {"value": 0, "fault": 1}.get(outcome, -1)
        problem = ""
        if cc.sanitizer_reports(r.stderr.decode("utf-8", "replace")) or r.stderr:
            problem = "stderr: %s" % " ".join(r.stderr.decode("utf-8", "replace").split())[:300]
        elif r.returncode != status:
            problem = "exit status %d, want %d" % (r.returncode, status)
        elif r.stdout != stdout:
            problem = "stdout %r, want %r" % (r.stdout[:80], stdout[:80])
        elif fuel_line(fuel) not in want:
            problem = "%s, want the %s" % (fuel_line(fuel), [line for line in want if line.startswith("fuel-")])
        elif status == 1:
            fmt = 2 if any(line.startswith("fault.source-map") for line in want) else 1
            try:
                _, rec = faults.decode_fault_record(record.read_bytes(), faults.RUN_TIME)
                lines = rec.to_expect_lines(fmt)
            except (OSError, ValueError) as ex:
                lines = ["unreadable record: %s" % ex]
            want_fault = [line for line in want if line.startswith("fault.")]
            if lines != want_fault:
                problem = "fault %s, want %s" % (lines, want_fault)
            elif (rec.revision or b"").hex() != revisions.get(name) or not revisions.get(name):
                problem = "fault.revision %r, want the program's SIR-17 revision %r" % (
                    rec.revision.hex() if rec.revision else None, revisions.get(name))
        item(label, problem)
    passed = sum(1 for _, p in items if not p)
    failures = ["%s: %s" % (label, p) for label, p in items if p]
    return passed, max(len(items), total_planned), failures
