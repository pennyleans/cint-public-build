"""The cint-as gate (docs/design/notes/2026-11-07-cint-as.md section 6).

    python toolchain/as/tests/test_as.py --cint CINT [--write-golden]

1. gen_cases.py --check: the generated cases match their generator.
2. Every cases/*.s assembles, and the SHA-256 of the object equals its row of golden.txt.
3. Oracle leg, when `as --version` is GNU as 2.42: `as --64` gives the same bytes, so golden.txt is
   GNU as 2.42's. Elsewhere the leg is reported as not run.
4. Every refuse/*.s exits 6, prints the one line its first line `# expect: ...` names, and writes no object.
5. The two tiers (#596; note section 5): a generated input of 5,469,953 bytes, over the small tier's 4 MiB,
   assembles under --frame-arena 134217728 to the pinned object (GNU as 2.47 with -mx86-used-note=no gives the same
   bytes); an input over the large tier's 32 MiB is refused before it is read, the limit and both arenas named.

--write-golden rewrites golden.txt from GNU as 2.42 objects, and refuses on any other `as`.
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
GOLDEN = HERE / "golden.txt"
GAS = "2.42"
SMALL_ARENA = "67108864"
LARGE_ARENA = "134217728"
LARGE_SHA256 = "68823c2ece6c73931036a14063a2d8060032d900f87b0e63852bdfba169996f2"
OVER = ("the input is over 33554432 bytes (4194304 assemble under --frame-arena 67108864, 33554432 under "
        "--frame-arena 134217728)")


def assemble(cint, source, obj, arena=SMALL_ARENA):
    return subprocess.run([str(cint), "run", "--root", ".", "--frame-arena", arena, "toolchain/as/main.ci",
                           "--", str(source), str(obj)], cwd=ROOT, capture_output=True, text=True)


def large_source():
    """5,469,953 bytes: 3,000 runs of 99 instructions, each ended by a jne back to its label, and 20,000 .quad."""
    lines = ["\t.text", "\t.globl big_start", "big_start:"]
    for k in range(3000):
        lines.append(f".L{k}:")
        lines += ["\tmovq %rax, %rbx", "\taddq $1, %rcx", "\tleaq 8(%rsp), %rdx"] * 33
        lines.append(f"\tjne .L{k}")
    lines.append("\tret")
    lines.append("\t.data")
    lines += [f"\t.quad {k * 7}" for k in range(20000)]
    return "\n".join(lines) + "\n"


def gas_version():
    path = shutil.which("as")
    if not path:
        return None
    try:
        first = subprocess.run([path, "--version"], capture_output=True, text=True).stdout.splitlines()
    except OSError:
        return None
    if not first or not first[0].startswith("GNU assembler"):
        return None
    return first[0].split()[-1]


def gas(source, obj):
    subprocess.run(["as", "--64", "-o", str(obj), str(source)], check=True, capture_output=True)
    return obj.read_bytes()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cint", type=Path, required=True)
    p.add_argument("--write-golden", action="store_true")
    a = p.parse_args()
    cases = sorted((HERE / "cases").glob("*.s"))
    refusals = sorted((HERE / "refuse").glob("*.s"))
    version = gas_version()
    oracle = version == GAS
    if a.write_golden:
        if not oracle:
            raise SystemExit(f"--write-golden needs GNU as {GAS}; found {version}")
        with tempfile.TemporaryDirectory(prefix="cint-as-") as t:
            rows = [f"{digest(gas(s, Path(t) / 'g.o'))}  {s.name}" for s in cases]
        GOLDEN.write_text("\n".join(rows) + "\n", encoding="ascii", newline="\n")
        print(f"wrote {GOLDEN.relative_to(ROOT)}: {len(rows)} rows from GNU as {version}")
        return
    bad = []
    generated = subprocess.run([sys.executable, str(HERE / "gen_cases.py"), "--check"], capture_output=True, text=True)
    if generated.returncode != 0:
        bad.append("gen_cases.py --check:\n" + generated.stdout)
    golden = {}
    for line in GOLDEN.read_text(encoding="ascii").splitlines():
        sha, name = line.split("  ")
        golden[name] = sha
    if sorted(golden) != [s.name for s in cases]:
        bad.append(f"golden.txt names {sorted(set(golden) ^ {s.name for s in cases})} without a case, or the reverse")
    with tempfile.TemporaryDirectory(prefix="cint-as-") as t:
        t = Path(t)
        for s in cases:
            obj = t / (s.stem + ".o")
            run = assemble(a.cint, s, obj)
            if run.returncode != 0 or not obj.exists():
                bad.append(f"{s.name}: exit {run.returncode}: {run.stdout.strip()} {run.stderr.strip()}")
                continue
            data = obj.read_bytes()
            if digest(data) != golden.get(s.name):
                bad.append(f"{s.name}: object {digest(data)} is not golden {golden.get(s.name)}")
            if oracle and gas(s, t / "g.o") != data:
                bad.append(f"{s.name}: not byte-equal to GNU as {GAS}")
        for s in refusals:
            want = s.read_text(encoding="ascii").splitlines()[0].removeprefix("# expect: ")
            obj = t / (s.stem + ".o")
            run = assemble(a.cint, s, obj)
            said = run.stdout.splitlines()
            if run.returncode != 6 or said[:1] != [f"cint-as: {want}"] or obj.exists():
                bad.append(f"refuse/{s.name}: exit {run.returncode}, said {said[:1]}, object {obj.exists()}; "
                           f"want exit 6, 'cint-as: {want}', no object")
        # 5. the tiers
        big = t / "large.s"
        big.write_text(large_source(), encoding="ascii", newline="\n")
        obj = t / "large.o"
        run = assemble(a.cint, big, obj, LARGE_ARENA)
        if run.returncode != 0 or not obj.exists() or digest(obj.read_bytes()) != LARGE_SHA256:
            bad.append(f"large tier: {big.stat().st_size} bytes: exit {run.returncode}, object "
                       f"{digest(obj.read_bytes()) if obj.exists() else None}; want exit 0, {LARGE_SHA256}")
        over = t / "over.s"
        with open(over, "wb") as f:
            f.truncate(33554433)
        obj = t / "over.o"
        run = assemble(a.cint, over, obj, LARGE_ARENA)
        said = run.stdout.splitlines()
        if run.returncode != 6 or said[:1] != [f"cint-as: {OVER}"] or obj.exists():
            bad.append(f"over the large tier: exit {run.returncode}, said {said[:1]}, object {obj.exists()}")
    for b in bad:
        print(f"FAIL {b}")
    leg = f"oracle leg against GNU as {GAS}: {'run' if oracle else f'not run (as is {version})'}"
    if bad:
        raise SystemExit(f"FAIL cint-as: {len(bad)} of {len(cases) + len(refusals) + 3} checks; {leg}")
    print(f"pass cint-as: {len(cases)} cases golden, {len(refusals)} refusals, both tiers; {leg}")


if __name__ == "__main__":
    main()
