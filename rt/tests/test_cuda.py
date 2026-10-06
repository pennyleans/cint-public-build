"""Build rt/cint_cuda.c on one leg and test it (box 11 unit 2; SPEC-03 A-16a, SPEC-02 B-16a,
B-16b, B-16e).

Usage (from the repository root):
    python rt/tests/test_cuda.py [--leg msvc|gcc|clang|apple-clang] [--out DIR] [--device]

Each leg builds rt/tests/cuda_fake.c as a shared library (a stand-in driver) and
rt/tests/test_cuda.c with rt/cint_cuda.c and CINT_CUDA_TESTING, under the strict warning
flags, at -O0 and -O2 (/Od and /O2); the gcc and clang legs also build both with
-fsanitize=address,undefined. Every build then runs the host protocol against the fake
driver and opens a driver library that does not exist. None of this needs a GPU or a CUDA
toolkit, and none of it touches a real driver.

`--device` adds the real driver: one unsanitized -O2 build opens the default driver library
and runs a hand-written PTX kernel over mapped, registered, and device memory, with launches
split and stopped above a fault, under block sizes 32 and 256. It needs an NVIDIA GPU
(natively on the msvc leg, or under WSL2 on gcc and clang); the box 11 note's BX11-19 keeps
it off the CI legs until a run shows the runner service reaches the GPU.

Build outputs go under <build>/cuda/<leg>/, where <build> is the CINT_BUILD environment
variable or cint-build in the system temporary directory. The script is a unittest module;
it uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_rt  # noqa: E402  (msvc_environment, compiler, flag lists)

ROOT = HERE.parents[1]
RT = ROOT / "rt"
SOURCES = [RT / "cint_cuda.c", HERE / "test_cuda.c"]

ARGS = argparse.Namespace(leg=None, out=None, device=False)
BUILDS: list[tuple[str, pathlib.Path, pathlib.Path]] = []  # (name, test program, fake driver)
STATE = {"leg": "msvc", "env": None, "device": None}


def default_out(leg: str) -> pathlib.Path:
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    return pathlib.Path(build) / "cuda" / leg


def configurations(leg: str):
    configs = [("O0", "0", False), ("O2", "2", False)]
    if leg in ("gcc", "clang"):
        configs += [("O0-san", "0", True), ("O2-san", "2", True)]
    return configs


def commands(leg, opt, sanitized, outdir: pathlib.Path):
    """The build commands of the fake driver and the test program, and their outputs."""
    if leg == "msvc":
        fake, exe = outdir / "cuda_fake.dll", outdir / "test_cuda.exe"
        common = [test_rt.ENV_CL[0], "/nologo", "/std:c17", "/W4", "/WX", "/Od" if opt == "0" else "/O2"]
        fake_cmd = [*common, "/LD", str(HERE / "cuda_fake.c"), f"/Fo{outdir}{os.sep}", f"/Fe{fake}"]
        test_cmd = [*common, "/DCINT_CUDA_TESTING", f"/I{RT}", *map(str, SOURCES), f"/Fo{outdir}{os.sep}",
                    f"/Fe{exe}"]
        return fake_cmd, fake, test_cmd, exe
    fake, exe = outdir / "libcuda-fake.so", outdir / "test_cuda"
    common = [test_rt.compiler(leg), *test_rt.GNU_WARNINGS, *(test_rt.CLANG_EXTRA if leg != "gcc" else []),
              f"-O{opt}", *(test_rt.SANITIZE if sanitized else [])]
    fake_cmd = [*common, "-shared", "-fPIC", str(HERE / "cuda_fake.c"), "-o", str(fake)]
    test_cmd = [*common, "-DCINT_CUDA_TESTING", f"-I{RT}", *map(str, SOURCES), "-o", str(exe),
                *(["-ldl"] if sys.platform.startswith("linux") else [])]
    return fake_cmd, fake, test_cmd, exe


def build(cmd, cwd, env, name):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode != 0 or (STATE["leg"] != "msvc" and r.stderr.strip()):
        raise RuntimeError(f"{name} build failed:\n{r.stdout}\n{r.stderr}")
    warnings = [line for line in r.stdout.splitlines() if re.search(r"warning [CD]\d+", line)]
    if warnings:
        raise RuntimeError(f"{name} build printed warnings:\n" + "\n".join(warnings))


def setUpModule():
    leg = ARGS.leg or ("msvc" if os.name == "nt" else "apple-clang" if sys.platform == "darwin" else "gcc")
    out = pathlib.Path(ARGS.out) if ARGS.out else default_out(leg)
    env = test_rt.msvc_environment() if leg == "msvc" else dict(os.environ)
    if leg == "msvc":
        test_rt.ENV_CL[0] = env["CINT_RT_CL"]
    STATE.update(leg=leg, env=env)
    for name, opt, sanitized in configurations(leg):
        outdir = out / name
        outdir.mkdir(parents=True, exist_ok=True)
        fake_cmd, fake, test_cmd, exe = commands(leg, opt, sanitized, outdir)
        build(fake_cmd, outdir, env, name + " fake driver")
        build(test_cmd, outdir, env, name)
        BUILDS.append((name, exe, fake))
        print(f"[{leg}] built {name}", flush=True)
    if ARGS.device:
        STATE["device"] = next(exe for name, exe, _ in BUILDS if name == "O2")


class CudaLayer(unittest.TestCase):
    def run_mode(self, exe: pathlib.Path, *argv: str) -> subprocess.CompletedProcess:
        return subprocess.run([str(exe), *argv], env=STATE["env"], capture_output=True, text=True, timeout=300)

    def test_fake_driver(self):
        for name, exe, fake in BUILDS:
            with self.subTest(config=name):
                r = self.run_mode(exe, "fake", str(fake))
                self.assertEqual(r.returncode, 0, f"{name}:\n{r.stdout}{r.stderr}")
                self.assertIn("PASS fake: 0 failed", r.stdout)

    def test_missing_driver(self):
        missing = str(pathlib.Path(tempfile.gettempdir()) / "cint-no-such-dir" / "libcuda.so.1")
        for name, exe, _ in BUILDS:
            with self.subTest(config=name):
                r = self.run_mode(exe, "missing", missing)
                self.assertEqual(r.returncode, 0, f"{name}:\n{r.stdout}{r.stderr}")

    def test_size_row(self):
        # SPEC-09 4.2: rt/cint_cuda.c, target 800 and ceiling 1,200 physical lines (BX11-08).
        data = (RT / "cint_cuda.c").read_bytes()
        self.assertLessEqual(data.count(b"\n"), 1200)
        for path in (RT / "cint_cuda.c", RT / "cint_cuda.h", HERE / "cuda_fake.c", HERE / "test_cuda.c"):
            self.assertNotIn(b"\r", path.read_bytes(), path.name)

    def test_device(self):
        if not ARGS.device:
            self.skipTest("needs --device and an NVIDIA GPU")
        exe = STATE["device"]
        r = self.run_mode(exe, "default")
        self.assertTrue(r.stdout.startswith("default ok"), f"the driver did not open:\n{r.stdout}{r.stderr}")
        r = self.run_mode(exe, "device")
        print(r.stdout, flush=True)
        self.assertEqual(r.returncode, 0, f"device:\n{r.stdout}{r.stderr}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"])
    parser.add_argument("--out")
    parser.add_argument("--device", action="store_true", help="also run the real driver on an NVIDIA GPU")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out, ARGS.device = known.leg, known.out, known.device
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
