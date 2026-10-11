"""A deterministic fuzz target over the decoder of a saved session (box 14 default BX14-20;
SPEC-06 13.2), named by its seed and count, as rt/tests/test_record.c's target is.

Run by workbench/tests/test_workbench.py (its units()) in one configuration of each leg, the
first sanitized one, or the first where the leg has none; with --cint PATH, against B1, which
compiles the entries of the session it saves. Each mutation edits
the manifest (CINT-SESSION-1) of that saved session or one of the objects it names: a session
checkpoint record, a recording, or an entry's text, which is then stored under its new digest.
`:open` of the mutation, then `:goto 2`, either opens it or refuses it, and the workbench goes on
to its next prompt and exits 0, with nothing on standard error (no sanitizer report).
"""
from __future__ import annotations

import hashlib
import pathlib
import re
import shutil
import tempfile

SEED = 0x5E551011
COUNT = 100
ENTRIES = """\
I64 x = 1;
export I64 f(I64 n) {
    x = x + n;
    return x;
}
:watch x
:run f(2) 2
:checkpoint one
x = 7;
:undo
:goto 2
f(1)
:save s.cis
"""
TOKENS = ["0", "1", "-1", "-", "4294967296", "18446744073709551616", "main", "../x.ci", "x" * 300, "0" * 64]


class Rng:
    """xorshift64, as the C fuzz targets use."""

    def __init__(self, seed: int):
        self.x = seed

    def next(self, n: int) -> int:
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
        x ^= x >> 7
        x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
        self.x = x
        return (x >> 8) % n


def mutated(rng: Rng, manifest: bytes, objects: pathlib.Path) -> tuple:
    """A mutation: the manifest's bytes, and an object to add under its digest, or None."""
    lines = manifest.split(b"\n")
    named = [w[-1] for w in (line.split(b" ") for line in lines) if w[0] not in (b"object", b"") and
             re.fullmatch(rb"[0-9a-f]{64}", w[-1])]
    how = rng.next(8)
    i = rng.next(len(lines))
    if how == 0:
        del lines[i]
    elif how == 1:
        lines.insert(i, lines[i])
    elif how == 2:
        j = rng.next(len(lines))
        lines[i], lines[j] = lines[j], lines[i]
    elif how == 3:
        words = lines[i].split(b" ")
        words[rng.next(len(words))] = TOKENS[rng.next(len(TOKENS))].encode("ascii")
        lines[i] = b" ".join(words)
    elif how == 4:
        data = bytearray(manifest)
        data[rng.next(len(data))] ^= 1 << rng.next(8)
        return bytes(data), None
    elif how == 5:
        return manifest[:rng.next(len(manifest))], None
    else:   # an object the manifest names, edited and stored again under its new digest
        old = named[rng.next(len(named))]
        data = bytearray((objects / old.decode("ascii")).read_bytes())
        at = rng.next(max(len(data), 1))
        if how == 6 and data:
            data[at] ^= 1 << rng.next(8)
        else:
            del data[at:]
        new = hashlib.sha256(bytes(data)).hexdigest().encode("ascii")
        return manifest.replace(old, new), bytes(data)
    return b"\n".join(lines), None


def check(case, wb, exes) -> int:
    """COUNT mutations of a saved session through :open; none without --cint, or in another
    configuration (the directory its executables were built in)."""
    configs = wb.configurations(case.leg)
    once = next((config for config, _, sanitized in configs if sanitized), configs[0][0])
    if wb.ARGS.cint is None or pathlib.Path(exes["test_wb"]).parent.name != once:
        return 0
    cint = pathlib.Path(wb.ARGS.cint).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        (work / "root").mkdir()
        workbench = [exes["cint-workbench"], "--cint", cint, "--root", work / "root", "--echo"]
        result = wb.run(workbench + ["--out", work / "session"], cwd=work, entries=ENTRIES)
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertIn(b"saved session to s.cis", result.stdout)
        manifest = (work / "s.cis" / "session.manifest").read_bytes()
        rng, opened = Rng(SEED), 0
        for n in range(COUNT):
            m = work / "m.cis"
            shutil.copytree(work / "s.cis", m)
            text, extra = mutated(rng, manifest, m / "objects")
            (m / "session.manifest").write_bytes(text)
            if extra is not None:
                (m / "objects" / hashlib.sha256(extra).hexdigest()).write_bytes(extra)
            result = wb.run(workbench + ["--out", work / "out"], cwd=work, entries=":open m.cis\n:goto 2\n")
            what = "seed %#x, mutation %d" % (SEED, n)
            case.assertClean(result, what)
            said = result.stdout.decode("utf-8", "replace")
            case.assertEqual((result.returncode, result.stderr), (0, b""), what)
            case.assertRegex(said, r"\nci\[[0-9]+\]> :goto 2\n(.*\n)*ci\[[0-9]+\]> \n?$", what)
            opened += "\nopened m.cis   " in said
            shutil.rmtree(m)
            shutil.rmtree(work / "out")
        print("wbt_open_fuzz: seed %#x, %d mutations, %d opened" % (SEED, COUNT, opened))
    return COUNT
