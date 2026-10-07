"""The type-signature decoder as a fuzz target (SPEC-09 CONF-16, SPEC-03 A-24).

Seeded mutations of valid signatures are decoded. Every input must either be
rejected with `SignatureError`, and nothing else, or decode to a signature
that encodes back to the same bytes and that a validator over the decoded
model, written apart from the decoder, accepts. Inputs that claim huge
counts must be rejected before the decoder allocates for them.

The normal test run uses a fixed seed and a few thousand inputs. A longer
run, from `python/`:

    python -m tests.test_sig_fuzz --iterations 1000000 --seed 7
"""
import argparse
import os
import random
import re
import struct
import sys
import time
import tracemalloc
import unittest

from cint import _sig
from cint._sig import (Array, Bound, Dim, EnumDef, EnumRef, ErrorSetDef, ErrorUnion, ErrorValue, Field, Parameter,
                       Scalar, Signature, SignatureError, StructDef, StructRef, Tuple)

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tests.test_sig import ADVANCE, BOTH, PAIRED, PARSE_U, READ_NUMBER  # noqa: E402

SEED = 20261005
ITERATIONS = 20000
SCALARS = ("Bool", "I8", "I16", "I32", "I64", "I128", "I1024", "U8", "U16", "U32", "U64", "Q32.32", "Q4.60",
           "Q4.4", "T1", "T27")
INTERESTING_U32 = (0, 1, 2, 3, 4, 5, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF)
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


# Valid signatures: the examples and a seeded generator.

def _ident(rng: random.Random, prefix: str) -> str:
    return prefix + "".join(rng.choice("abcxyz_019") for _ in range(rng.randint(0, 6)))


def _dim(rng: random.Random, size_count: int, literal: bool = False) -> Dim:
    form = 1 if literal else rng.randint(0, 3)
    def bound():
        if literal or size_count == 0 or rng.random() < 0.3:
            return Bound(None, rng.randint(0, 9))
        return Bound(rng.randrange(size_count), rng.randint(-2, 2))
    if form == 0:
        return Dim(0)
    if form == 1:
        return Dim(1, bound=bound())
    return Dim(form, rng.randint(-3, 3), bound())


def generate(rng: random.Random) -> Signature:
    """A signature that A-24 accepts: definitions in depth-first order, each
    referred to, with parameters and a result that refer to them."""
    defs, uses = [], []
    if rng.random() < 0.5:
        fields = tuple(Field("f%d" % i, 0, Scalar(rng.choice(SCALARS))) for i in range(rng.randint(1, 3)))
        defs.append(StructDef(_ident(rng, "m.S"), rng.random() < 0.3, fields))
        if rng.random() < 0.5:
            arr = Array(Scalar("I8"), (_dim(rng, 0, literal=True),))
            defs.append(StructDef(_ident(rng, "m.T"), False, (Field("inner", 0, StructRef(len(defs) - 1)),
                                                             Field("bits", 1, Scalar("Bool")), Field("raw", 0, arr))))
        uses.append(StructRef(len(defs) - 1))
    if rng.random() < 0.4:
        underlying = rng.choice(("I8", "I32", "U16"))
        low = 0 if underlying.startswith("U") else -1
        values = tuple(("e%d" % i, low + i) for i in range(rng.randint(1, 4)))
        defs.append(EnumDef(_ident(rng, "m.E"), underlying, values))
        uses.append(EnumRef(len(defs) - 1))
    result = rng.choice((None, Scalar(rng.choice(SCALARS)), Tuple((Scalar("I64"), Scalar("Bool")))))
    if rng.random() < 0.5:
        a = len(defs)
        defs.append(ErrorSetDef(_ident(rng, "m.A"), "U8", tuple("a%d" % i for i in range(rng.randint(1, 3)))))
        index = a
        if rng.random() < 0.5:
            defs.append(ErrorSetDef(_ident(rng, "m.B"), "U16", ("b0", "b1")))
            defs.append(ErrorSetDef(_ident(rng, "m.C"), "U16", members=(a, a + 1)))
            index = a + 2
        result = ErrorUnion(index, rng.choice((None, Scalar("I64"), Tuple((Scalar("U8"), Scalar("I32"))))))
    size_count = rng.randint(0, 3)
    params = [Parameter("in", u) for u in uses]
    for _ in range(rng.randint(0, 4)):
        if rng.random() < 0.5:
            params.append(Parameter("in", Scalar(rng.choice(SCALARS))))
        else:
            elem = Scalar(rng.choice(SCALARS)) if not uses or rng.random() < 0.7 else rng.choice(uses)
            dims = tuple(_dim(rng, size_count) for _ in range(rng.randint(1, 4)))
            params.append(Parameter(rng.choice(_sig.MODES), Array(elem, dims)))
    rng.shuffle(params)
    sig = Signature(tuple(defs), size_count, tuple(params), result)
    return _sig.decode(_sig.encode(sig))          # the generator's own output must decode


def corpus(rng: random.Random, extra: int = 40) -> list:
    out = [ADVANCE, BOTH, READ_NUMBER, _sig.encode(PAIRED), _sig.encode(PARSE_U)]
    while len(out) < 5 + extra:
        try:
            out.append(_sig.encode(generate(rng)))
        except SignatureError:
            continue
    return out


# Mutations.

def mutate(rng: random.Random, data: bytes, pool: list) -> bytes:
    b = bytearray(data)
    for _ in range(rng.choice((1, 1, 1, 2, 3, 8))):
        op = rng.randrange(9)
        at = rng.randrange(len(b) + 1) if b else 0
        if op == 0 and b:
            i = rng.randrange(len(b))
            b[i] ^= 1 << rng.randrange(8)
        elif op == 1 and b:
            b[rng.randrange(len(b))] = rng.choice((0x00, 0x01, 0x02, 0x04, 0x05, 0x14, 0x31, 0x52, 0x71, 0x72, 0x73,
                                                   0x74, 0x76, 0x7F, 0x80, 0xFF, rng.randrange(256)))
        elif op == 2:
            b[at:at] = bytes([rng.randrange(256)])
        elif op == 3 and b:
            del b[rng.randrange(len(b))]
        elif op == 4:
            del b[at:]
        elif op == 5 and len(b) >= 4:
            i = rng.randrange(len(b) - 3)
            b[i:i + 4] = struct.pack("<I", rng.choice(INTERESTING_U32 + (len(b), len(b) - i, rng.randrange(1 << 32))))
        elif op == 6 and b:
            i = rng.randrange(len(b))
            j = min(len(b), i + rng.randint(1, 16))
            b[at:at] = b[i:j]
        elif op == 7:
            other = rng.choice(pool)
            i = rng.randrange(len(other) + 1)
            b = b[:at] + bytearray(other[i:])
        elif op == 8 and len(b) >= 8:
            i = rng.randrange(len(b) - 7)
            b[i:i + 8] = struct.pack("<q", rng.choice((0, 1, -1, 1 << 62, -(1 << 63), (1 << 63) - 1)))
    return bytes(b)


# A validator over the decoded model, apart from the decoder's code.

_WIDTH = {"Bool": 1, "I8": 8, "I16": 16, "I32": 32, "I64": 64, "U8": 8, "U16": 16, "U32": 32, "U64": 64,
          "T1": 8, "T27": 64}


def _scalar_ok(name: str) -> bool:
    if name in _WIDTH or name in ("I128", "I256", "I512", "I1024"):
        return True
    m = re.match(r"Q([0-9]+)\.([0-9]+)\Z", name)
    return bool(m) and int(m.group(1)) + int(m.group(2)) in (8, 16, 32, 64) and int(m.group(1)) >= 1


def validate(sig: Signature) -> list:
    """The A-24 obligations a decoded model breaks, as messages (none for a
    valid signature)."""
    problems = []
    used = set()
    kinds = {StructRef: StructDef, EnumRef: EnumDef, ErrorValue: ErrorSetDef, ErrorUnion: ErrorSetDef}

    def ref(t, limit):
        if not 0 <= t.index < limit:
            problems.append("reference %d at or above %d" % (t.index, limit))
            return
        if not isinstance(sig.definitions[t.index], kinds[type(t)]):
            problems.append("reference %d of the wrong kind" % t.index)
        used.add(t.index)

    def check(t, where, limit):
        if isinstance(t, Scalar):
            if not _scalar_ok(t.elem):
                problems.append("scalar %s" % t.elem)
        elif isinstance(t, Array):
            if where not in ("param", "field"):
                problems.append("array in %s" % where)
            if not 1 <= len(t.dims) <= 4:
                problems.append("rank %d" % len(t.dims))
            if isinstance(t.elem, (Array, Tuple, ErrorUnion)):
                problems.append("array element %s" % type(t.elem).__name__)
            check(t.elem, "element", limit)
            for d in t.dims:
                if where == "field" and (d.form != 1 or d.bound.symbol is not None):
                    problems.append("field array extent")
                if d.bound is not None and d.bound.symbol is not None and d.bound.symbol >= sig.size_count:
                    problems.append("size symbol %d" % d.bound.symbol)
        elif isinstance(t, Tuple):
            if where not in ("result", "success") or len(t.items) < 2:
                problems.append("tuple in %s with %d items" % (where, len(t.items)))
            for x in t.items:
                if isinstance(x, (Array, Tuple, ErrorUnion)):
                    problems.append("tuple item %s" % type(x).__name__)
                check(x, "item", limit)
        elif isinstance(t, ErrorUnion):
            if where != "result":
                problems.append("error union in %s" % where)
            ref(t, limit)
            if t.value is not None:
                if isinstance(t.value, (Array, ErrorUnion)):
                    problems.append("success type %s" % type(t.value).__name__)
                check(t.value, "success", limit)
        else:
            ref(t, limit)

    seen = set()
    for i, d in enumerate(sig.definitions):
        parts = d.name.split(".")
        if not all(_IDENT.match(p) and len(p) <= 255 for p in parts):
            problems.append("qualified name %r" % d.name)
        key = (type(d), d.name)
        if key in seen:
            problems.append("definition %s twice" % d.name)
        seen.add(key)
        if isinstance(d, StructDef):
            names = [f.name for f in d.fields]
            if len(set(names)) != len(names):
                problems.append("field names of %s" % d.name)
            for f in d.fields:
                if not _IDENT.match(f.name) or len(f.name) > 255:
                    problems.append("field %r" % f.name)
                check(f.type, "field", i)
                if f.bits:
                    if isinstance(f.type, (Array, StructRef)):
                        problems.append("bit width on %s" % type(f.type).__name__)
                    elif isinstance(f.type, Scalar) and f.type.elem == "Bool" and f.bits != 1:
                        problems.append("Bool bit width %d" % f.bits)
                    elif isinstance(f.type, Scalar) and f.bits > _WIDTH.get(f.type.elem, 1024):
                        problems.append("bit width %d" % f.bits)
        elif isinstance(d, EnumDef):
            if d.underlying not in ("I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"):
                problems.append("enum underlying %s" % d.underlying)
            names = [n for n, _ in d.values]
            if len(set(names)) != len(names) or len({v for _, v in d.values}) != len(d.values):
                problems.append("enumerators of %s" % d.name)
        else:
            if d.underlying not in ("U8", "U16", "U32", "U64"):
                problems.append("set underlying %s" % d.underlying)
            capacity = (1 << _WIDTH[d.underlying]) - 1
            if d.combined:
                if len(set(d.members)) != len(d.members):
                    problems.append("member twice in %s" % d.name)
                total = 0
                for m in d.members:
                    if not 0 <= m < i or not isinstance(sig.definitions[m], ErrorSetDef) \
                            or sig.definitions[m].combined:
                        problems.append("member %d of %s" % (m, d.name))
                        continue
                    used.add(m)
                    total += len(sig.definitions[m].values)
                if total > capacity:
                    problems.append("capacity of %s" % d.name)
            else:
                if len(set(d.values)) != len(d.values) or len(d.values) > capacity:
                    problems.append("values of %s" % d.name)
    n = len(sig.definitions)
    for p in sig.params:
        if p.mode not in _sig.MODES:
            problems.append("mode %r" % p.mode)
        check(p.type, "param", n)
    if sig.result is not None:
        check(sig.result, "result", n)
    if used != set(range(n)):
        problems.append("unreferenced definitions %s" % sorted(set(range(n)) - used))
    return problems


# The property.

def run(seed: int, iterations: int, report=None) -> dict:
    """Decode `iterations` mutated inputs; raises AssertionError at the first
    input that breaks the property, naming the seed, the iteration, and the
    bytes."""
    rng = random.Random(seed)
    pool = corpus(rng)
    stats = {"accepted": 0, "rejected": 0}
    for k in range(iterations):
        data = mutate(rng, rng.choice(pool), pool)
        try:
            sig = _sig.decode(data)
        except SignatureError as e:
            if not 0 <= e.offset <= len(data):
                raise AssertionError("seed %d input %d: offset %d outside %d bytes: %s"
                                     % (seed, k, e.offset, len(data), data.hex()))
            stats["rejected"] += 1
            continue
        except Exception as e:      # anything else is a decoder defect
            raise AssertionError("seed %d input %d: %s: %s on %s" % (seed, k, type(e).__name__, e, data.hex()))
        again = _sig.encode(sig)
        if again != data:
            raise AssertionError("seed %d input %d: accepted bytes do not encode back: %s" % (seed, k, data.hex()))
        problems = validate(sig)
        if problems:
            raise AssertionError("seed %d input %d: accepted a signature that breaks A-24 (%s): %s"
                                 % (seed, k, "; ".join(problems), data.hex()))
        names = tuple("p%d" % i for i in range(len(sig.params)))
        _sig.render(sig, "f", tuple("s%d" % i for i in range(sig.size_count)), names)
        try:
            _sig.python_forms(sig, names)
        except (_sig.NoPythonForm, ValueError):
            pass
        _sig.error_sets(sig)
        stats["accepted"] += 1
        if len(pool) < 400 and rng.random() < 0.1:
            pool.append(data)       # accepted mutants seed further mutation
        if report is not None and k and k % 100000 == 0:
            report(k, stats)
    return stats


class Fuzz(unittest.TestCase):

    def test_generator_and_validator_agree(self):
        rng = random.Random(SEED)
        for _ in range(300):
            sig = generate(rng)
            self.assertEqual(validate(sig), [])
            self.assertEqual(_sig.decode(_sig.encode(sig)), sig)

    def test_examples_validate(self):
        for data in (ADVANCE, BOTH, READ_NUMBER, _sig.encode(PAIRED), _sig.encode(PARSE_U)):
            self.assertEqual(validate(_sig.decode(data)), [])

    def test_seeded_mutations(self):
        stats = run(SEED, ITERATIONS)
        self.assertGreater(stats["rejected"], ITERATIONS // 4)
        self.assertGreater(stats["accepted"], ITERATIONS // 50)   # mutants that stay valid exercise the round trip

    def test_huge_counts_allocate_nothing(self):
        """A-24: a count larger than the bytes remaining is rejected before the
        decoder allocates for what it claims."""
        p = _sig.PREFIX
        claims = [
            p + struct.pack("<I", 0xFFFFFFFF),
            p + struct.pack("<III", 0, 0, 0xFFFFFFFF),
            p + struct.pack("<I", 1) + b"\x71" + struct.pack("<I", 0xFFFFFFF0),
            p + struct.pack("<I", 1) + b"\x71" + struct.pack("<I", 3) + b"m.S\x00" + struct.pack("<I", 0x7FFFFFFF),
            p + struct.pack("<I", 1) + b"\x73" + struct.pack("<I", 3) + b"m.E\x24\x00" + struct.pack("<I", 0xFFFFFFFF),
            p + struct.pack("<I", 1) + b"\x73" + struct.pack("<I", 3) + b"m.E\x24\x01" + struct.pack("<I", 0xFFFFFFFF),
            p + struct.pack("<III", 0, 0, 0) + b"\x76" + struct.pack("<I", 0xFFFFFFFF),
        ]
        for data in claims:
            tracemalloc.start()
            try:
                with self.assertRaises(SignatureError):
                    _sig.decode(data)
                _, peak = tracemalloc.get_traced_memory()
            finally:
                tracemalloc.stop()
            self.assertLess(peak, 64 * 1024, data.hex())


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Fuzz the type-signature decoder (SPEC-09 CONF-16).")
    parser.add_argument("--iterations", type=int, default=1_000_000)
    parser.add_argument("--seed", type=int, default=None, help="default: a random seed, printed")
    args = parser.parse_args(argv)
    seed = args.seed if args.seed is not None else int.from_bytes(os.urandom(4), "little")
    start = time.monotonic()
    print("seed %d, %d inputs" % (seed, args.iterations), flush=True)

    def report(k, stats):
        print("  %d inputs: %d accepted, %d rejected" % (k, stats["accepted"], stats["rejected"]), flush=True)
    try:
        stats = run(seed, args.iterations, report)
    except AssertionError as failure:
        print("FAIL %s" % failure)
        return 1
    print("pass: %d accepted, %d rejected in %d s" % (stats["accepted"], stats["rejected"],
                                                     int(time.monotonic() - start)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
