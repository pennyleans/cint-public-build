"""The gate of the static linker (docs/design/notes/2026-11-07-static-linker.md section 5).

python3 link/tests/test_link.py [--cint PATH] [--keep DIR]

Linux only: gcc's musl wrapper (musl-gcc), musl's libc.a and crt objects (MUSL_LIB, or a standard
place), and GNU ld as the reference. Without --cint (or CINT_EXE) a cint is bootstrapped from this
tree. Every case is linked twice from the same objects, by link/ld.ci and by `ld -static`, and both
executables run with the same arguments, environment and directory:

  LK-1  the C cases of link/tests/c/: stdout, stderr and exit status agree
  LK-2  cint programs (examples, and link/ld.ci itself) through `cint emit-c`: the same
  LK-3  the linker as linked by itself relinks every case: each output equals the first linker's, byte for byte
  LK-4  the refusals of link/tests/refuse/ and a file that is not an object: a nonzero exit, the named
        error, and no output file
  LK-5  the tiers (#597): 45 inputs, past the old 32; 17 MiB of input and output, past the small tier, through
        a linker built for the large tier's arena; both run as GNU ld's do. 65 MiB of input is refused unread
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ARENA = "67108864"
LARGE_ARENA = "134217728"
EXAMPLES = ("hello", "ledger", "overflow_hunt")
MUSL_PLACES = ("/usr/lib/musl/lib", "/usr/lib/x86_64-linux-musl", "/usr/local/musl/lib")
FAILURES = []


def say(ok, what):
    print(("pass " if ok else "FAIL ") + what, flush=True)
    if not ok:
        FAILURES.append(what)


def sh(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, **kw)


def musl_lib():
    for place in ([os.environ["MUSL_LIB"]] if "MUSL_LIB" in os.environ else []) + list(MUSL_PLACES):
        lib = Path(place)
        if all((lib / name).is_file() for name in ("crt1.o", "crti.o", "crtn.o", "libc.a")):
            return lib
    sys.exit("test_link: no musl libc.a and crt objects (install musl-tools or set MUSL_LIB)")


def cint_exe(work):
    given = os.environ.get("CINT_EXE")
    if given:
        return Path(given)
    boot = work / "boot"
    subprocess.run([sys.executable, "tools/cint_bootstrap.py", "--leg", "gcc", "--out", str(boot)], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    return boot / "cint"


def include_dir(cint):
    for line in (cint.parent / "cint.toolchain").read_text().splitlines():
        key, _, value = line.partition(" ")
        if key == "include":
            return Path(value)
    sys.exit("test_link: the toolchain file names no include directory")


def compile_c(sources, flags, out):
    """Objects of the C sources, one each, with gcc's musl wrapper."""
    out.mkdir(parents=True, exist_ok=True)
    objects = []
    for i, src in enumerate(sources):
        obj = out / f"{i:02d}-{Path(src).stem}.o"
        sh(["musl-gcc", *flags, "-c", src, "-o", obj])
        objects.append(obj)
    return objects


def run(exe, args, cwd):
    cwd.mkdir(parents=True, exist_ok=True)
    p = subprocess.run([str(exe), *args], cwd=cwd, env={"PATH": "/usr/bin:/bin", "LK": "gate"},
                       stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def link_ours(ld, out, inputs, cwd=None):
    p = subprocess.run([str(ld), str(out), *map(str, inputs)], capture_output=True, cwd=cwd)
    if p.returncode == 0:
        os.chmod(out, 0o755)
    return p


def link_gnu(out, inputs):
    sh(["ld", "-static", "-nostdlib", "-o", out, *inputs])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cint")
    parser.add_argument("--keep")
    a = parser.parse_args()
    if a.cint:
        os.environ["CINT_EXE"] = a.cint
    if not sys.platform.startswith("linux"):
        sys.exit("test_link: Linux only")
    work = Path(a.keep) if a.keep else Path(tempfile.mkdtemp(prefix="cint-link-"))
    if work.exists() and a.keep:
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)
    try:
        gate(work)
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    if FAILURES:
        print(f"test_link: {len(FAILURES)} failed: " + ", ".join(FAILURES))
        return 1
    print("pass static linker gate: LK-1 to LK-5")
    return 0


def gate(work):
    lib = musl_lib()
    crt = [lib / "crt1.o", lib / "crti.o"]
    tail = [lib / "libc.a", lib / "crtn.o"]
    cint = cint_exe(work)
    inc = include_dir(cint)

    # stage 1: the linker as cint builds it
    sh([cint, "build", "--app", "--frame-arena", ARENA, "--root", ROOT, "--out", work / "s1", "link/ld.ci"], cwd=ROOT)
    s1 = work / "s1" / "ld"

    # the cases: name -> (inputs, program arguments)
    cases = {}
    for case in sorted(p for p in (HERE / "c").iterdir() if p.is_dir()):
        flags_file = case / "flags"
        flags = flags_file.read_text().split() if flags_file.exists() else ["-O2", "-fno-pie"]
        objects = compile_c(sorted(case.glob("*.c")), ["-std=c17", *flags], work / "obj" / case.name)
        cases["c/" + case.name] = (crt + objects + tail, ["alpha", "beta"])

    # the runtime as one prebuilt object, as BU-4 ships it
    rt_flags = ["-std=c17", "-O2", "-fPIC", "-DCINT_RT_HELPERS_PORTABLE", f"-I{inc}"]
    rt = compile_c([inc / "cint_rt.c"], rt_flags, work / "obj" / "rt")
    programs = {name: f"examples/{name}.ci" for name in EXAMPLES}
    programs["ld"] = "link/ld.ci"
    for name, module in programs.items():
        stage = work / "emit" / name
        sh([cint, "emit-c", "--root", ROOT, "--out", stage, module], cwd=ROOT)
        emitted = stage.parent / (stage.name + ".stage-0")
        sources = sorted(emitted.rglob("*.c"))
        objects = compile_c(sources, rt_flags, work / "obj" / ("ci-" + name))
        cases["ci/" + name] = (crt + objects + rt + tail, [])
    hello_inputs = cases["c/stdio"][0]
    cases["ci/ld"] = (cases["ci/ld"][0], ["--frame-arena", ARENA, "--", "a.out", *map(str, hello_inputs)])

    # LK-1, LK-2: each case linked both ways, run both ways
    outputs = {}
    for name, (inputs, args) in cases.items():
        d = work / "link" / name
        d.mkdir(parents=True)
        ours, ref = d / "ours", d / "ref"
        p = link_ours(s1, ours, inputs)
        if p.returncode != 0:
            say(False, f"{'LK-2' if name.startswith('ci/') else 'LK-1'} {name}: ld.ci exited {p.returncode}: "
                + p.stdout.decode(errors="replace").strip())
            continue
        link_gnu(ref, inputs)
        got = run(ours, args, d / "run-ours")
        want = run(ref, args, d / "run-ref")
        same = got == want
        if name == "ci/ld":
            a_out, b_out = d / "run-ours" / "a.out", d / "run-ref" / "a.out"
            same = same and a_out.is_file() and a_out.read_bytes() == b_out.read_bytes()
        unit = "LK-2" if name.startswith("ci/") else "LK-1"
        detail = f"status {got[0]}, {len(got[1])} stdout bytes" if same else f"ours {got} ld {want}"
        say(same, f"{unit} {name}: {detail}")
        outputs[name] = ours

    # LK-3: the self-linked linker relinks every case, byte for byte
    s2 = outputs.get("ci/ld")
    if s2 is None:
        say(False, "LK-3: no self-linked linker")
    else:
        for name, (inputs, _) in cases.items():
            if name not in outputs:
                continue
            again = work / "relink" / name.replace("/", "-")
            again.parent.mkdir(parents=True, exist_ok=True)
            p = subprocess.run([str(s2), "--frame-arena", ARENA, "--", str(again), *map(str, inputs)],
                               capture_output=True)
            same = p.returncode == 0 and again.read_bytes() == outputs[name].read_bytes()
            say(same, f"LK-3 {name}: {outputs[name].stat().st_size} bytes" if same
                else f"LK-3 {name}: status {p.returncode} {p.stdout!r}")

    # LK-4: refusals
    for case in sorted(p for p in (HERE / "refuse").iterdir() if p.is_dir()):
        objects = compile_c(sorted(case.glob("*.c")), ["-std=c17", "-O2", "-fno-pie"], work / "obj" / ("refuse-" + case.name))
        refuse(s1, work / "refused" / case.name, crt + objects + tail, (case / "expect").read_text().strip(), case.name)
    junk = work / "obj" / "junk.o"
    junk.write_bytes(b"not an object\n")
    refuse(s1, work / "refused" / "junk", crt + [junk] + tail, "not an x86-64 ELF relocatable object", "not an object")

    # LK-5: the tiers
    sh([cint, "build", "--app", "--frame-arena", LARGE_ARENA, "--root", ROOT, "--out", work / "s1big", "link/ld.ci"],
       cwd=ROOT)
    s1big = work / "s1big" / "ld"
    gen = work / "gen"
    many = gen / "many"
    many.mkdir(parents=True)
    sources = [many / "main.c"]
    for k in range(40):
        f = many / f"f{k:02d}.c"
        f.write_text(f"int lk5_f{k:02d}(int x) {{ return x * {k + 1}; }}\n")
        sources.append(f)
    (many / "main.c").write_text("#include <stdio.h>\n" + "".join(f"int lk5_f{k:02d}(int);\n" for k in range(40))
                                 + "int main(void)\n{\n    long s = 0;\n"
                                 + "".join(f"    s += lk5_f{k:02d}({k});\n" for k in range(40))
                                 + "    printf(\"%ld\\n\", s);\n    return 0;\n}\n")
    objects = compile_c(sources, ["-std=c17", "-O2", "-fno-pie"], work / "obj" / "lk5-many")
    both("45 inputs", s1, crt + objects + tail, work / "link" / "lk5-many")
    big = gen / "big"
    big.mkdir(parents=True)
    (big / "big.c").write_text("const unsigned char lk5_big[17u << 20] = {1, 2, 3};\n")
    (big / "main.c").write_text("#include <stdio.h>\nextern const unsigned char lk5_big[17u << 20];\n"
                                "int main(void)\n{\n    printf(\"%u %zu\\n\", lk5_big[0] + lk5_big[1] + lk5_big[2] + "
                                "lk5_big[(17u << 20) - 1], sizeof lk5_big);\n    return 0;\n}\n")
    objects = compile_c([big / "main.c", big / "big.c"], ["-std=c17", "-O2", "-fno-pie"], work / "obj" / "lk5-big")
    both("17 MiB of input", s1big, crt + objects + tail, work / "link" / "lk5-big")
    over = gen / "over.o"
    with open(over, "wb") as f:
        f.truncate(65 << 20)
    refuse(s1, work / "refused" / "over", crt + [over] + tail, "ld: limit: the inputs are larger than 64 MiB",
           "65 MiB of input", "LK-5")


def both(name, ld, inputs, d):
    """LK-5: inputs linked by ld and by GNU ld; both executables give the same output and status."""
    d.mkdir(parents=True)
    ours, ref = d / "ours", d / "ref"
    p = link_ours(ld, ours, inputs)
    if p.returncode != 0:
        say(False, f"LK-5 {name}: ld.ci exited {p.returncode}: " + p.stdout.decode(errors="replace").strip())
        return
    link_gnu(ref, inputs)
    got = run(ours, [], d / "run-ours")
    want = run(ref, [], d / "run-ref")
    size = sum(Path(i).stat().st_size for i in inputs)
    say(got == want, f"LK-5 {name}: {len(inputs)} inputs, {size} bytes, {ours.stat().st_size} bytes out, "
        + (f"status {got[0]}, {got[1]!r}" if got == want else f"ours {got} ld {want}"))


def refuse(ld, out, inputs, expect, name, unit="LK-4"):
    out.parent.mkdir(parents=True, exist_ok=True)
    p = link_ours(ld, out, inputs)
    text = p.stdout.decode(errors="replace")
    ok = p.returncode != 0 and expect in text and not out.exists()
    say(ok, f"{unit} {name}: {text.strip()}" if ok else f"{unit} {name}: status {p.returncode}, said {text!r}")


if __name__ == "__main__":
    sys.exit(main())
