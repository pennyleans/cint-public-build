"""Deterministic fuzz targets for the two decoders that freeze with fault record version 2.

Slice 2 decision patch D-10 (decision 23) freezes the IM-149 fault
record at T2 stage acceptance "with each decoder (the IM-149 reader in
`cint_ref` and the `.expect` reader) with a deterministic fuzz target whose
random seed and count the receipt records". The targets:

- `record`: `cint_ref.faults.decode_fault_record` (SPEC-01 IM-148, IM-149),
  under both record kinds (D-17: the caller passes compile-time or run-time).
- `expect`: `cint_ref.expect.parse` (SPEC-09 CONF-11, formats 1 and 2).

Each target mutates a fixed seed corpus with a pseudo-random generator seeded
by `--seed`, `--count` times. The generator is Python's `random.Random`, used
only through `getrandbits`, whose output for an integer seed is fixed by its
documented reproducibility rule; draws below a bound use rejection sampling,
written here. No floating point. Properties checked on every input:

- record: the reader raises nothing but `ValueError`; a record it accepts
  re-encodes to the same bytes (`encode_fault_record`, same kind and version);
  every record accepted under the run-time kind is accepted, equal, under the
  compile-time kind, whose bounds are wider (IM-108).
- expect: the reader raises nothing but `ValueError`; a text it accepts
  renders back to the same text; an accepted format 2 text holds as many
  `fault.stack` lines as `fault.stack-depth` says (CONF-11 rule 5).

Usage, from the repository root:
    python conformance/tools/fuzz_decoders.py [--seed N] [--count N] [--target record|expect|all]
                                              [--receipt FILE]

It prints one line per target, `fuzz <target>: seed S, count N, accepted A,
rejected R, failures F, inputs-sha256 H`, where H is the SHA-256 of every
generated input in order (so two runs with one seed can be compared), and
exits 1 if any property fails. `--receipt FILE` also writes those fields as
JSON (`schema` `cint-fuzz-1`, SPEC-06 section 15) for a stage receipt.
Python standard library only; LF only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "ref"))

from cint_ref import expect as X  # noqa: E402
from cint_ref import faults as F  # noqa: E402
from cint_ref.types import Value  # noqa: E402

DEFAULT_SEED = 20261003
DEFAULT_COUNT = 20000
INTERESTING = (0, 1, 2, 8, 20, 32, 64, 255, 256, 257, 258, 512, 513, 514, 519, 520, 521, 1024, 1025,
               0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)


class Draw:
    """Integers from random.Random(seed).getrandbits only."""

    def __init__(self, seed: int):
        self.rng = random.Random(seed)

    def below(self, n: int) -> int:
        if n <= 1:
            return 0
        k = (n - 1).bit_length()
        while True:
            v = self.rng.getrandbits(k)
            if v < n:
                return v

    def pick(self, seq):
        return seq[self.below(len(seq))]


def _rec(code, op, operands, exact, limit, pos, stack=(), revision=None, address=None, source_map=None):
    return F.FaultRecord(code, op, tuple(operands), exact, limit, F.Position(*pos), tuple(F.Position(*p) for p in stack),
                         revision, address, source_map)


def record_corpus() -> list:
    """(bytes, kind) pairs: valid records of both versions and kinds, with every
    field form the reader knows, and the D-17 bound cases."""
    i64max = (1 << 63) - 1
    conf01 = _rec("E_OVERFLOW", "add.checked.i64", [Value("I64", i64max), Value("I64", 1)], 1 << 63,
                  Value("I64", i64max), ("arith/add_i64_overflow.ci", 5, 15))
    stack = _rec("E_DEPTH", "call.enter", [], None, Value("I64", 4), ("control/depth_limit_stack.ci", 20, 12),
                 [("control/depth_limit_stack.ci", 8, 12), ("control/depth_limit_stack.ci", 12, 12)])
    wide = _rec("E_NARROW", "unassigned", [Value("Z", 1 << 3000)], 1 << 3000, Value("I8", 127),
                ("arith/const_narrow_literal_2p3000.ci", 8, 762))
    widest = _rec("E_NARROW", "unassigned", [Value("Z", -((1 << 4096) - 1))], -((1 << 4096) - 1),
                  Value("I8", -128), ("arith/const_narrow_literal_max.ci", 8, 1036))
    q_exact = _rec("E_NARROW", "unassigned", [Value("Z", (1 << 4096) - 1)], ((1 << 4096) - 1) << 63,
                   F.RawValue(bytes([0x31, 0x14, 63, 0]) + ((1 << 63) - 1).to_bytes(8, "little")),
                   ("t.ci", 1, 1))
    kinds = _rec("E_BOUNDS", "index.checked", [Value("Bool", True), Value("U8", 255), Value("I1024", -1),
                                               F.RawValue(bytes([0x51, 0x11, 1]) + (3).to_bytes(8, "little") + b"\x78\x78\x88"),
                                               F.RawValue(bytes([0x61, 0x14]) + bytes(16) + b"\x01" + bytes(8)
                                                          + (4).to_bytes(8, "little") + bytes(8) + (1).to_bytes(8, "little")
                                                          + b"\x00"),
                                               F.RawValue(b"\x7f\x14")],
                 F.RawValue(b"\x7f\x0f"), Value("I64", 3), ("k/mod.ci", 2, 9), revision=bytes(range(32)),
                 address=F.Address("k.kern", 7, 1, 3, 2), source_map=bytes(range(32, 64)))
    out = []
    for r in (conf01, stack, kinds):
        for version in (1, 2):
            if version == 1 and r.source_map is not None:
                continue
            out.append((F.encode_fault_record(r, F.RUN_TIME, version), F.RUN_TIME))
    for r in (wide, widest, q_exact):
        out.append((F.encode_fault_record(r, F.COMPILE_TIME, 2), F.COMPILE_TIME))
    return out


def expect_corpus() -> list:
    """Every frozen `.expect` text of the suite, sorted by path."""
    texts = []
    for d, _, files in sorted(os.walk(os.path.join(ROOT, "conformance"))):
        for name in sorted(files):
            if name.endswith(".expect"):
                with open(os.path.join(d, name), "rb") as fh:
                    texts.append(fh.read())
    return texts


def mutate_bytes(d: Draw, b: bytes, donors) -> bytes:
    b = bytearray(b)
    for _ in range(1 + d.below(3)):
        op = d.below(7)
        n = len(b)
        if op == 0 and n:                                   # replace a byte
            b[d.below(n)] = d.below(256)
        elif op == 1:                                       # truncate
            del b[d.below(n + 1):]
        elif op == 2:                                       # insert bytes
            at = d.below(n + 1)
            b[at:at] = bytes(d.below(256) for _ in range(1 + d.below(4)))
        elif op == 3 and n:                                 # delete a range
            at = d.below(n)
            del b[at:at + 1 + d.below(8)]
        elif op == 4 and n:                                 # duplicate a range
            at = d.below(n)
            b[at:at] = b[at:at + 1 + d.below(16)]
        elif op == 5 and n >= 4:                            # a U32 length or count field
            at = d.below(n - 3)
            b[at:at + 4] = d.pick(INTERESTING).to_bytes(4, "little")
        elif op == 6:                                       # splice from another input
            other = d.pick(donors)
            at, src = d.below(n + 1), d.below(len(other) + 1)
            b[at:] = other[src:]
    return bytes(b)


def mutate_text(d: Draw, t: bytes, donors) -> bytes:
    if d.below(3) == 0:
        return mutate_bytes(d, t, donors)
    lines = t.split(b"\n")
    for _ in range(1 + d.below(3)):
        op = d.below(6)
        n = len(lines)
        i = d.below(n)
        if op == 0:                                         # delete a line
            del lines[i]
        elif op == 1:                                       # duplicate a line
            lines.insert(i, lines[i])
        elif op == 2 and n > 1:                             # swap two lines
            j = d.below(n)
            lines[i], lines[j] = lines[j], lines[i]
        elif op == 3:                                       # a line from another file
            other = d.pick(donors).split(b"\n")
            lines.insert(i, d.pick(other))
        elif op == 4:                                       # a format 2 line
            lines.insert(i, d.pick([b"format 2", b"format 1", b"fault.source-map self",
                                    b"fault.stack a/b.ci:1:1", b"state.global a.b x I64 1",
                                    b"state.global x I64 1"]))
        else:                                               # a value made edge-case
            key, _, _ = lines[i].partition(b" ")
            lines[i] = key + d.pick([b" ", b"  0", b" 0 ", b" -0", b" \r", b"\t1", b" 2", b" 99999999999999999999"])
        if not lines:
            lines = [b""]
    return b"\n".join(lines)


def fuzz_record(seed: int, count: int):
    d = Draw(seed)
    corpus = record_corpus()
    donors = [b for b, _ in corpus]
    digest = hashlib.sha256()
    accepted = rejected = 0
    failures = []
    for n in range(len(corpus) + count):
        if n < len(corpus):
            data, kind = corpus[n]
        else:
            base, kind = d.pick(corpus)
            data = mutate_bytes(d, base, donors)
            kind = d.pick(F.RECORD_KINDS) if d.below(4) == 0 else kind
        digest.update(len(data).to_bytes(4, "little") + data + kind.encode("ascii"))
        try:
            version, rec = F.decode_fault_record(data, kind)
        except ValueError:
            rejected += 1
            if n < len(corpus):
                failures.append("seed input %d is rejected" % n)
            continue
        except Exception as e:                              # any other exception is a reader defect
            failures.append("input %d: %s: %s" % (n, type(e).__name__, e))
            continue
        accepted += 1
        try:
            again = F.encode_fault_record(rec, kind, version)
            if again != data:
                failures.append("input %d: accepted bytes do not re-encode identically" % n)
            if kind == F.RUN_TIME and F.decode_fault_record(data, F.COMPILE_TIME) != (version, rec):
                failures.append("input %d: accepted run-time, not equal under compile-time" % n)
        except Exception as e:
            failures.append("input %d: %s: %s" % (n, type(e).__name__, e))
    return accepted, rejected, failures, digest.hexdigest()


def fuzz_expect(seed: int, count: int):
    d = Draw(seed)
    corpus = expect_corpus()
    digest = hashlib.sha256()
    accepted = rejected = 0
    failures = []
    for n in range(len(corpus) + count):
        data = corpus[n] if n < len(corpus) else mutate_text(d, d.pick(corpus), corpus)
        digest.update(len(data).to_bytes(4, "little") + data)
        try:
            e = X.parse(data.decode("ascii"))
        except (ValueError, UnicodeDecodeError):
            rejected += 1
            if n < len(corpus):
                failures.append("frozen file %d is rejected" % n)
            continue
        except Exception as ex:
            failures.append("input %d: %s: %s" % (n, type(ex).__name__, ex))
            continue
        accepted += 1
        if X.render(e).encode("ascii") != data:
            failures.append("input %d: accepted text does not render identically" % n)
        if e.format == 2 and e.get("outcome") == "fault" and \
                int(e.get("fault.stack-depth")) != len(e.getall("fault.stack")):
            failures.append("input %d: a format 2 stack count differs from its lines" % n)
    return accepted, rejected, failures, digest.hexdigest()


TARGETS = {"record": fuzz_record, "expect": fuzz_expect}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED)
    ap.add_argument("--count", type=int, default=DEFAULT_COUNT)
    ap.add_argument("--target", choices=["record", "expect", "all"], default="all")
    ap.add_argument("--receipt", help="also write the results as JSON to this file")
    ns = ap.parse_args(argv)
    if ns.count < 0 or ns.seed < 0:
        ap.error("--seed and --count are integers >= 0")
    results = []
    for name in (["record", "expect"] if ns.target == "all" else [ns.target]):
        accepted, rejected, failures, h = TARGETS[name](ns.seed, ns.count)
        for f in failures[:20]:
            print("failure %s: %s" % (name, f))
        print("fuzz %s: seed %d, count %d, accepted %d, rejected %d, failures %d, inputs-sha256 %s"
              % (name, ns.seed, ns.count, accepted, rejected, len(failures), h))
        results.append({"target": name, "seed": ns.seed, "count": ns.count, "accepted": accepted,
                        "rejected": rejected, "failures": len(failures), "inputs_sha256": h})
    if ns.receipt:
        decoders = {}
        for rel in ("ref/cint_ref/faults.py", "ref/cint_ref/expect.py", "conformance/tools/fuzz_decoders.py"):
            with open(os.path.join(ROOT, rel), "rb") as fh:
                decoders[rel] = hashlib.sha256(fh.read()).hexdigest()
        with open(ns.receipt, "wb") as fh:
            fh.write((json.dumps({"schema": "cint-fuzz-1", "targets": results, "sources_sha256": decoders},
                                 indent=2, sort_keys=True) + "\n").encode("ascii"))
    return 1 if any(r["failures"] for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
