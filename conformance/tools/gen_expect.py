"""Write the `.expect` file of every program case with `python -m cint_ref run`.

A program case is a `.ci` file under one of the category directories. Its
header comment names the entry, the arguments and the budgets, because
SPEC-09 9.2 does not yet say where a case states them (ref/OPEN.md O-10):

    // case: arith/seed08_runtime_overflow
    // clause: SPEC-09 SEED-08 row 4; SPEC-01 4.1, 9.2
    // entry: run; args: none[; fuel N][; depth N]

A case without an `entry` line names no entry: `cint_ref` runs the script,
else `main`, else the only test (ref/OPEN.md REF-OQ-12). The CONF-01 example
(`arith/add_i64_overflow.ci`) keeps the SPEC-09 listing unchanged; its header
is folded into its first line and it runs its only test block.

For each case this runs `python -m cint_ref run <case.ci> --case ... --clause
... [--entry NAME] [--arg ...] [--fuel N] [--depth N]` and writes the output
bytes, unchanged, to `<case>.expect`. A case whose header names several
entries gets one file per entry, `<case>.<entry>.expect`, with the case name
`<case>.<entry>` (ref/OPEN.md I-3).

The `.expect` format (SPEC-09 CONF-11) is the frozen file's own: a file whose
third line is `format 2`, `format 3` or `format 4` is regenerated and checked in
that format, any other in format 1. A case without a file is written in the
format `--format` names (default 1); `--format` never changes the format of an
existing file. Format 3 (BX12-21) is for a case whose entry returns an error,
which no earlier format can write (CONF-11 rule 12); format 4 (box 09 ruling R9)
is for a case whose fault is raised by a kernel dispatch.

A `boot/` refusal case with a `seed outcome` header line is a program of
`cint-core-1` that the seed must refuse (SPEC-09 BOOT-01, SEED-06). Its
`.expect` holds the seed's outcome, compared on code and position (SPEC-09
CONF-14 category 4; OQ-154, decided by D-8). The outcome comes from the
specification, not from the seed: the header line

    // seed outcome: C9100 at 9:13

names the code and the position, the first character of the first
construct outside `cint-boot-1`, and the `.expect` is written from it with
source `compile-error`. `cint_ref` still runs the case, which must not be a
compile error under `cint-core-1` (it may be refused where `cint_ref` does
not implement the construct). Files in subdirectories of a category, such
as `module/lib/`, are imported modules, not cases.

Such a case also has an expectation for `cintc`, which has no outside-the-
subset category (SPEC-09 CONF-11 as proposed under decision 26, item 2;
compiler/OPEN.md CINTC-OQ-48): `<case>.cintc.expect`, `cint_ref`'s outcome
in the CONF-11 grammar, written and checked like any reference file. Where
`cint_ref` refuses the program, no such file can be frozen (CONF-11 rule 7),
and held.txt lists `<case>.cintc` instead. The seed's file is not changed.

`conformance/held.txt` lists the cases held out of the frozen set, each with
the ref/OPEN.md entries it cites. A listed case is not run: it is skipped and
its reason printed. Every cited entry must exist as a `### <id>.` heading in
ref/OPEN.md, and every listed case must exist. A case that `cint_ref` refuses
or cannot run (array arguments), but that held.txt does not list, is reported
as unlisted and gets no `.expect` file.

Usage (from the repository root):
    python conformance/tools/gen_expect.py [--check] [--format 1|2|3|4] [case.ci ...]

`--check` writes nothing and exits 1 if any existing `.expect` differs from a
fresh run, if a held case still has an `.expect` file, or if a case is held
without being listed. Python standard library only; no floating point; LF
only.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFORMANCE = os.path.join(ROOT, "conformance")
CATEGORIES = ("arith", "control", "switch", "struct", "diag", "determinism", "boot", "module", "view",
              "reduce", "kernel", "errors", "cleanup", "ternary", "memory")
SEED_OUTCOME = re.compile(r"^// seed outcome: (C[0-9]{4}) at ([1-9][0-9]*):([1-9][0-9]*)$", re.M)
HELD = os.path.join(CONFORMANCE, "held.txt")
CINTC = ".cintc"   # the per-compiler expectation of a boot/ refusal case (CONF-11, decision 26)
OPEN_MD = os.path.join(ROOT, "ref", "OPEN.md")


def read_held(path=HELD, open_md=OPEN_MD):
    """held.txt as {case: (reason ids, reason)}; raises ValueError on a bad line."""
    with open(path, "rb") as fh:
        text = fh.read().decode("ascii")
    if "\r" in text:
        raise ValueError("%s: CR found; LF only" % path)
    with open(open_md, "rb") as fh:
        ids = set(re.findall(r"^### ([A-Z]+(?:-[A-Z]+)*-[0-9]+)\.", fh.read().decode("utf-8"), re.M))
    held = {}
    for n, line in enumerate(text.split("\n"), start=1):
        if not line or line.startswith("#"):
            continue
        parts = line.split(" ", 2)
        if len(parts) != 3:
            raise ValueError("held.txt line %d: expected `<case> <reason ids> <reason>`" % n)
        case, refs, reason = parts
        for ref in refs.split(","):
            if ref not in ids:
                raise ValueError("held.txt line %d: %s is not an entry of ref/OPEN.md" % (n, ref))
        base = case[:-len(CINTC)] if case.endswith(CINTC) else case
        if not os.path.exists(os.path.join(CONFORMANCE, base + ".ci")):
            raise ValueError("held.txt line %d: no case %s" % (n, base))
        if base != case and not base.startswith("boot/"):
            raise ValueError("held.txt line %d: %s names the cintc expectation of a case that "
                             "is not a boot/ refusal case" % (n, case))
        if case in held:
            raise ValueError("held.txt line %d: %s listed twice" % (n, case))
        held[case] = (refs, reason)
    return held


def header(text):
    """The case header as a dict: case, clause, entries, args, fuel, depth."""
    h = {}
    if text.startswith("﻿"):           # diag/c1002_byte_order_mark: the header follows the mark
        text = text[1:]
    for line in text.split("\n"):
        m = re.match(r"// (case|clause|entry): (.*)\Z", line)
        if m and m.group(1) not in h:
            h[m.group(1)] = m.group(2).strip()
    if "case" in h and "entry" not in h:     # no named entry (REF-OQ-12)
        return {"case": h["case"], "clause": h.get("clause"), "entries": [None], "args": [],
                "fuel": None, "depth": None}
    if "case" not in h:                      # the CONF-01 example: header folded into line 1
        first = text.split("\n", 1)[0]
        m = re.search(r"clause ([^;]*);", first)
        return {"case": None, "clause": m.group(1).strip() if m else None,
                "entries": [None], "args": [], "fuel": None, "depth": None}
    parts = [p.strip() for p in h["entry"].split(";")]
    out = {"case": h["case"], "clause": h.get("clause"), "fuel": None, "depth": None, "args": []}
    out["entries"] = [e.strip() for e in parts[0].split(",")]
    for p in parts[1:]:
        if p.startswith("args:"):
            a = p[len("args:"):].strip()
            if a != "none":
                out["args"] = [x.strip() for x in a.split(",")] if "[" not in a else [a]
        elif p.startswith("fuel "):
            out["fuel"] = p[len("fuel "):].strip()
        elif p.startswith("depth "):
            out["depth"] = p[len("depth "):].strip()
        else:
            raise ValueError("unknown header field %r" % p)
    return out


def seed_expect(text, h, case_id):
    """The `.expect` bytes of a boot/ refusal case, from its `seed outcome` header line, or
    None when the case is not one (its subset is cint-boot-1, or it has no such line)."""
    m = SEED_OUTCOME.search(text)
    if not case_id.startswith("boot/") or "// subset: cint-boot-1" in text or m is None:
        return None
    lines = ["case " + (h["case"] or case_id)]
    if h["clause"]:
        lines.append("clause " + h["clause"])
    lines += ["source compile-error", "outcome compile-error", "diagnostic.code " + m.group(1),
              "diagnostic.position %s.ci:%s:%s" % (case_id, m.group(2), m.group(3))]
    return ("\n".join(lines) + "\n").encode("ascii")



def expect_format(path):
    """The CONF-11 format of an existing `.expect` file: 2, 3 or 4 when its third line is
    `format 2`, `format 3` or `format 4`, else 1; None when the file does not exist."""
    if not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        lines = fh.read().split(b"\n", 3)
    if len(lines) > 3 and lines[2] in (b"format 2", b"format 3", b"format 4"):
        return int(lines[2][len(b"format "):])
    return 1


def run_case(ci_path, h, entry, fmt=1):
    case = h["case"] or os.path.relpath(ci_path, CONFORMANCE).replace(os.sep, "/")[:-3]
    if entry is not None and len(h["entries"]) > 1:
        case = "%s.%s" % (case, entry)
    cmd = [sys.executable, "-m", "cint_ref", "run", ci_path, "--case", case]
    if h["clause"]:
        cmd += ["--clause", h["clause"]]
    if entry is not None:
        cmd += ["--entry", entry]
    for a in h["args"]:
        cmd += ["--arg", a]
    if h["fuel"] is not None:
        cmd += ["--fuel", h["fuel"]]
    if h["depth"] is not None:
        cmd += ["--depth", h["depth"]]
    if fmt != 1:
        cmd += ["--format", str(fmt)]
    env = dict(os.environ, PYTHONPATH=os.path.join(ROOT, "ref"))
    r = subprocess.run(cmd, capture_output=True, env=env, cwd=ROOT)
    return r.returncode, r.stdout, r.stderr


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--format", type=int, choices=(1, 2, 3, 4), default=1,
                    help="the .expect format of a case that has no file yet (CONF-11)")
    ns = ap.parse_args(argv)
    paths = ns.cases or sorted(p for c in CATEGORIES for p in glob.glob(os.path.join(CONFORMANCE, c, "*.ci")))
    held_list = read_held()
    written = held = differ = unlisted = 0
    for ci in paths:
        ci = os.path.abspath(ci)
        with open(ci, "rb") as fh:
            text = fh.read().decode("utf-8", "replace")
        h = header(text)
        case_id = os.path.relpath(ci, CONFORMANCE).replace(os.sep, "/")[:-3]
        for entry in h["entries"]:
            base = ci[:-3] + ("" if len(h["entries"]) == 1 else "." + entry)
            out_path = base + ".expect"
            rel = os.path.relpath(out_path, ROOT).replace(os.sep, "/")
            if case_id in held_list:
                held += 1
                refs, reason = held_list[case_id]
                print("held %s: %s %s" % (rel, refs, reason))
                if os.path.exists(out_path):
                    differ += 1
                    print("differs %s: the case is held but has an .expect file" % rel)
                continue
            why = None
            seed = seed_expect(text, h, case_id)
            if any("[" in a for a in h["args"]):
                why = "array arguments: a case list has no form for them before T3 (OQ-133)"
            else:
                code, out, err = run_case(ci, h, entry, expect_format(out_path) or ns.format)
                if code != 0 or not out:
                    why = "cint_ref exited %d: %s" % (code, err.decode("ascii", "replace").strip()[-200:])
                elif seed is not None:
                    ref_out = out
                    if b"\noutcome compile-error\n" in out:
                        why = "cint_ref rejects the program, which a boot/ refusal case must not: " + \
                            out.decode("ascii").rsplit("diagnostic.code ", 1)[-1].split("\n")[0]
                    out = seed
                elif b"\noutcome refused\n" in out:
                    why = "refused: " + out.decode("ascii").rsplit("refused.reason ", 1)[-1].strip()
            if why is not None:
                unlisted += 1
                print("unlisted %s: not in held.txt and not runnable: %s" % (rel, why))
                continue
            files = [(out_path, rel, out)]
            if seed is not None:
                # The cintc expectation of a boot/ refusal case: cint_ref's outcome, or held.
                c_path = base + CINTC + ".expect"
                c_rel = os.path.relpath(c_path, ROOT).replace(os.sep, "/")
                c_fmt = expect_format(c_path)
                if c_fmt not in (None, 1):
                    code, c_out, err = run_case(ci, h, entry, c_fmt)
                else:
                    c_out = ref_out
                if case_id + CINTC in held_list:
                    held += 1
                    refs, reason = held_list[case_id + CINTC]
                    print("held %s: %s %s" % (c_rel, refs, reason))
                    if os.path.exists(c_path):
                        differ += 1
                        print("differs %s: the cintc expectation is held but has a file" % c_rel)
                elif b"\noutcome refused\n" in c_out:
                    unlisted += 1
                    print("unlisted %s: not in held.txt and not runnable: refused: %s" % (
                        c_rel, c_out.decode("ascii").rsplit("refused.reason ", 1)[-1].strip()))
                else:
                    files.append((c_path, c_rel, c_out))
            for path, path_rel, data in files:
                if ns.check:
                    old = open(path, "rb").read() if os.path.exists(path) else None
                    if old != data:
                        differ += 1
                        print("differs %s" % path_rel)
                    continue
                with open(path, "wb") as fh:
                    fh.write(data)
                written += 1
    if ns.check:
        print("check: %d differ, %d held, %d unlisted" % (differ, held, unlisted))
        return 1 if differ or unlisted else 0
    print("written %d, held %d, unlisted %d" % (written, held, unlisted))
    return 1 if unlisted else 0


if __name__ == "__main__":
    sys.exit(main())
