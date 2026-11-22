"""Probe kernels on cpu-qbe (SIMD-7; SPEC-09 BACK-18; compiler/back_qbe.ci w_knode, w_kcall, w_kcx).

python compiler/tests/qbe_kernels.py --cint /path/to/cint --out /external/dir [--qbe PATH]

Each probe is built twice, by cpu-c17 (the toolchain beside cint) and by cpu-qbe, and run once by
cint_ref. The three agree on the outcome kind, stdout and fuel; a fault's record decodes to the
reference's lines, and its bytes from the two builds are equal. The probes reach what the 53
conformance cases of the kernel refusal do not: max, count and sum_wrap, a U64 sum (the exact
accumulator's direct path), an I64 sum that carries into it, wide and fixed-point accumulators of
every reduction, more than four `over` variables (the division path of work-item indices), a
dispatch whose arrays are view parameters, whose scalar `out` is a module variable or a wide
local, an I8 `in`, and a fault in each phase of a dispatch.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ref"))
sys.path.insert(0, str(ROOT / "tools"))
from cint_qbe_check import toolchain
from cint_ref.exec import run_program
from cint_ref.faults import RUN_TIME, decode_fault_record


# The probes of the independent review of PR 884 (findings 2 to 6), each killing a mutant the
# earlier probes left live: a function whose only module state is a dispatch's `out` (uses_state's
# form-8 clause), a wide sum read back into a narrow `out` past a stained cell (w_kepi's load at
# the type's width), U64 contributions into a wide total (their tag), and a fixed-point
# fold_checked fault (the fraction bits of cint_qk_kfold_q_<s>).
REVIEW_VALID = (
    ("state_via_dispatch", """\
I32 g = 5;

kernel ks[n](in I32[n] x, out I32 s) over [i: n] {
    reduce s = sum(x[i]);
}

void put() {
    I32[3] x = [1, 2, 3];
    ks(x, g);
}

void main() {
    put();
    "{g}\\n";
}
"""),
    ("wide_into_i32_stained", """\
kernel wn2[n](in I128[n] x, in I64[n] y, out I128 t, out I32 s) over [i: n] {
    reduce t = sum(y[i]);
    reduce s = sum(x[i]);
}

void main() {
    I128[3] x = [-5, 2147483647, -2147483000];
    I64[3] y = [1, 2, -1];
    I128 t = 0;
    I32 s = 7;
    wn2(x, y, t, s);
    I64 z = s as I64;
    "{t} {s} {z}\\n";
    I128[3] x2 = [-5, -2147483000, 3];
    wn2(x2, y, t, s);
    z = s as I64;
    "{t} {s} {z}\\n";
}
"""),
    ("u64_into_u128", """\
kernel su[n](in U64[n] x, out U128 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    U64[3] x = [0xFFFFFFFFFFFFFFFF, 0x8000000000000000, 3];
    U128 s = 0;
    su(x, s);
    "{s}\\n";
}
"""),
)

REVIEW_FAULTS = (
    ("fixed_fold_overflow", """\
kernel fq[n](in Q16.16[n] x, out Q16.16 s) over [i: n] {
    reduce s = fold_checked(add, 0.0, x[i]);
}

void main() {
    Q16.16[3] x = [20000.0, 20000.0, 1.0];
    Q16.16 s = 0.0;
    fq(x, s);
    "{s}\\n";
}
"""),
)

# Exported kernels cpu-qbe refuses with capability `entry` (kcx_ok): a wide scalar `in`, which
# cpu-c17 builds, and a wide scalar `out`, whose cx wrapper would pass an 8-byte cell for 16 bytes
# (finding 3 of the independent review of PR 884). cpu-c17 does not compile the second: its
# wrapper initializes the cint_i128 `out` with `= 0` (back_c.c_public_kernel), a defect of its own.
REFUSED = (
    ("export_wide_in", True, """\
export kernel kw[n](in I64[n] x, in I128 b, out I64 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I64[3] x = [1, 2, 3];
    I64 s = 0;
    kw(x, 5, s);
    "{s}\\n";
}
"""),
    ("export_wide_out", False, """\
export kernel kw[n](in I64[n] x, out I128 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I64[3] x = [1, 2, 3];
    I128 s = 0;
    kw(x, s);
    "{s}\\n";
}
"""),
)


VALID = (
    ("red_narrow", """\
kernel red8[n](in I8[n] x, out I8 mx, out I8 mn, out I64 cnt, out I8 sw, out I8 ss, out I64 fc, out I64 sm)
        over [i: n] {
    reduce mx = max(x[i]);
    reduce mn = min(x[i]);
    reduce cnt = count(x[i] > 0);
    reduce sw = sum_wrap(x[i]);
    reduce ss = sum_sat(x[i]);
    reduce fc = fold_checked(add, 7, x[i] as I64);
    reduce sm = sum(x[i]);
}

kernel redu[n](in U64[n] x, in U32[n] y, out U64 mx, out U64 mn, out U32 my, out U64 s, out U32 sw)
        over [i: n] {
    reduce mx = max(x[i]);
    reduce mn = min(x[i]);
    reduce my = max(y[i]);
    reduce s = sum(x[i]);
    reduce sw = sum_wrap(y[i]);
}

kernel carry[n](in I64[n] x, out I64 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I8[6] x = [100, -128, 127, 5, -1, 90];
    I8 mx = 0;
    I8 mn = 0;
    I64 cnt = 0;
    I8 sw = 0;
    I8 ss = 0;
    I64 fc = 0;
    I64 sm = 0;
    red8(x, mx, mn, cnt, sw, ss, fc, sm);
    "{mx} {mn} {cnt} {sw} {ss} {fc} {sm}\\n";
    U64[3] u = [0x8000000000000000, 0x7FFFFFFFFFFFFFFE, 1];
    U32[3] v = [4000000000, 5, 3000000000];
    U64 umx = 0;
    U64 umn = 0;
    U32 vmx = 0;
    U64 us = 0;
    U32 vsw = 0;
    redu(u, v, umx, umn, vmx, us, vsw);
    "{umx} {umn} {vmx} {us} {vsw}\\n";
    I64[5] c = [9223372036854775807, 9223372036854775807, -9223372036854775807, -9223372036854775807, 5];
    I64 cs = 0;
    carry(c, cs);
    "{cs}\\n";
}
"""),
    ("red_wide", """\
kernel wred[n](in I128[n] x, in U256[n] y, out I128 mx, out I128 mn, out I128 sw, out I128 ss,
        out U256 s, out I256 sn) over [i: n] {
    reduce mx = max(x[i]);
    reduce mn = min(x[i]);
    reduce sw = sum_wrap(x[i]);
    reduce ss = sum_sat(x[i]);
    reduce s = sum(y[i]);
    reduce sn = sum(x[i]);
}

kernel wfold[n](in I128[n] x, out I128 fc) over [i: n] {
    reduce fc = fold_checked(add, 3, x[i]);
}

kernel wnarrow[n](in I32[n] x, out I128 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I128 big = 170141183460469231731687303715884105727;
    I128[3] x = [big, -5, 7];
    U256[3] y = [1, 2, 3];
    I128 mx = 0;
    I128 mn = 0;
    I128 sw = 0;
    I128 ss = 0;
    U256 s = 0;
    I256 sn = 0;
    wred(x, y, mx, mn, sw, ss, s, sn);
    "{mx} {mn} {sw} {ss} {s} {sn}\\n";
    I128[3] f = [-5, big, -7];
    I128 fc = 0;
    wfold(f, fc);
    "{fc}\\n";
    I32[3] z = [2147483647, 2147483647, 2147483647];
    I128 t = 0;
    wnarrow(z, t);
    "{t}\\n";
}
"""),
    ("red_fixed", """\
kernel qred[n](in Q16.16[n] x, out Q16.16 mx, out Q16.16 mn, out Q16.16 sw, out Q16.16 ss, out Q16.16 fc,
        out Q16.16 s) over [i: n] {
    reduce mx = max(x[i]);
    reduce mn = min(x[i]);
    reduce sw = sum_wrap(x[i]);
    reduce ss = sum_sat(x[i]);
    reduce fc = fold_checked(add, 0.5, x[i]);
    reduce s = sum(x[i]);
}

void main() {
    Q16.16[4] x = [1.5, -0.25, 2.0, -3.75];
    Q16.16 mx = 0.0;
    Q16.16 mn = 0.0;
    Q16.16 sw = 0.0;
    Q16.16 ss = 0.0;
    Q16.16 fc = 0.0;
    Q16.16 s = 0.0;
    qred(x, mx, mn, sw, ss, fc, s);
    "{mx} {mn} {sw} {ss} {fc} {s}\\n";
}
"""),
    ("dispatch_args", """\
I32 gsum = 7;
I128 gwide = 9;

kernel ksum[n](in I32[n] x, out I32 s, out I128 w) over [i: n] {
    reduce s = sum(x[i]);
    reduce w = sum(x[i]);
}

kernel kscale[h, w](in I32[h, w] a, in I32 k, in I8 neg, out I32[h, w] b) over [y: h, x: w] {
    I32 v = a[y, x] * k;
    if (neg != 0) {
        v = -v;
    }
    b[y, x] = v;
}

kernel kbump[n](inout I64[n] a) over [i: n] {
    a[i] = a[i] + (i as I64);
}

void via_views(in I32[_] xs, inout I64[_] ys, in I32[_, _] g) {
    I32 s = 0;
    I128 w = 0;
    ksum(xs, s, w);
    ksum(xs, gsum, gwide);
    kbump(ys);
    I32[2, 3] res;
    kscale(g, 3, -1, res);
    "{s} {w} {gsum} {gwide} {ys[0]} {ys[3]} {res[0, 0]} {res[1, 2]}\\n";
}

void main() {
    I32[4] xs = [1, 2, 3, 4];
    I64[4] ys = [10, 20, 30, 40];
    I32[2, 3] g;
    g[0, 0] = 5;
    g[1, 2] = -6;
    via_views(xs, ys, g);
    I32[2, 3] direct;
    kscale(g, 2, 0, direct);
    "{direct[0, 0]} {direct[1, 2]}\\n";
}
"""),
    ("over_five", """\
kernel five[a, b, c, d, e](in I64[a, b, c, d] x, in I64[e] y, out I64 s) over [i: a, j: b, k: c, l: d, m: e] {
    reduce s = sum(x[i, j, k, l] * y[m] + (i * 10000 + j * 1000 + k * 100 + l * 10 + m));
}

void main() {
    I64[2, 2, 3, 2] x;
    x[1, 1, 2, 1] = 5;
    x[0, 1, 0, 1] = -3;
    I64[3] y = [1, 2, 3];
    I64 s = 0;
    five(x, y, s);
    "{s}\\n";
}
"""),
    ("red_q128", """\
kernel q128[n](in Q80.48[n] x, out Q80.48 mx, out Q80.48 mn, out Q80.48 sw, out Q80.48 ss)
        over [i: n] {
    reduce mx = max(x[i]);
    reduce mn = min(x[i]);
    reduce sw = sum_wrap(x[i]);
    reduce ss = sum_sat(x[i]);
}

void main() {
    Q80.48[4] x = [1.5, -0.25, 1000000.125, -3.75];
    Q80.48 mx = 0.0;
    Q80.48 mn = 0.0;
    Q80.48 sw = 0.0;
    Q80.48 ss = 0.0;
    q128(x, mx, mn, sw, ss);
    "{mx} {mn} {sw} {ss}\\n";
}
"""),
    ("globals_adjacent", """\
I32 ga = 7;
I32 gb = -9;
I16 gc = 5;
I16 gd = -3;

kernel kn[n](in I32[n] x, out I32 s, out I16 t) over [i: n] {
    reduce s = sum(x[i]);
    reduce t = sum(x[i] as I16);
}

void main() {
    I32[3] x = [1, 2, 3];
    kn(x, ga, gc);
    "{ga} {gb} {gc} {gd}\\n";
}
"""),
    ("wide_into_narrow", """\
kernel wn[n](in I128[n] x, out I64 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I128[3] x = [9223372036854775807, 9223372036854775807, -9223372036854775807];
    I64 s = 0;
    wn(x, s);
    "{s}\\n";
}
"""),
    ("module_arrays", """\
I64[4] src = [5, 6, 7, 8];
I64[4] dst;
I64 tot = 0;

kernel twice[n](in I64[n] a, out I64[n] b, out I64 t) over [i: n] {
    b[i] = a[i] * 2;
    reduce t = sum(a[i] * 2);
}

void main() {
    twice(src, dst, tot);
    "{dst[0]} {dst[3]} {tot}\\n";
}
"""),
    ("u64_wrap_wide_in", """\
kernel uw[n](in U64[n] a, in I128 bias, in Q16.16 q, out U64 w, out I128 s, out Q16.16 m) over [i: n] {
    reduce w = sum_wrap(a[i]);
    reduce s = sum((a[i] as I128) + bias);
    reduce m = max(q);
}

void main() {
    U64[2] a = [0xFFFFFFFFFFFFFFFF, 3];
    U64 w = 0;
    I128 s = 0;
    Q16.16 m = 0.0;
    uw(a, -1, 2.5, w, s, m);
    "{w} {s} {m}\\n";
}
"""),
)

FAULTS = (
    ("f_shape_entry", """\
kernel add2[n](in I64[n] a, in I64[n] b, out I64[n] c) over [i: n] {
    c[i] = a[i] + b[i];
}

void go(in I64[_] a, in I64[_] b, inout I64[_] c) {
    add2(a, b, c);
}

void main() {
    I64[3] a = [1, 2, 3];
    I64[4] b = [1, 2, 3, 4];
    I64[3] c;
    go(a, b, c);
    "{c[0]}\\n";
}
"""),
    ("f_where_lt", """\
kernel kl[n](in I64[n] a, out I64 s) over [i: n] where n < 3 {
    reduce s = sum(a[i]);
}

void go(in I64[_] a) {
    I64 s = 0;
    kl(a, s);
    "{s}\\n";
}

void main() {
    I64[3] a = [1, 2, 3];
    go(a[0..2]);
    go(a);
}
"""),
    ("f_index_item", """\
kernel ix[n](in I64[n] a, in I64 k, out I64 s) over [i: n] {
    reduce s = sum(a[i + k]);
}

void main() {
    I64[3] a = [1, 2, 3];
    I64 s = 0;
    ix(a, 1, s);
    "{s}\\n";
}
"""),
    ("f_div_counters", """\
kernel q[h, w](in I64[h, w] a, out I64[h, w] b) over [y: h, x: w] {
    b[y, x] = 100 / a[y, x];
}

void main() {
    I64[2, 3] a;
    a[0, 0] = 1;
    a[0, 1] = 2;
    a[0, 2] = 3;
    a[1, 0] = 4;
    I64[2, 3] b;
    q(a, b);
    "{b[0, 0]}\\n";
}
"""),
    ("f_max_empty", """\
kernel hi[n](in I64[n] x, out I64 m) over [i: n] {
    if (x[i] > 100) {
        reduce m = max(x[i]);
    }
}

void main() {
    I64[3] x = [1, 2, 3];
    I64 m = 0;
    hi(x, m);
    "{m}\\n";
}
"""),
    ("f_fold_i8", """\
kernel f8[n](in I8[n] x, out I8 s) over [i: n] {
    reduce s = fold_checked(add, 0, x[i]);
}

void main() {
    I8[3] x = [100, 27, 1];
    I8 s = 0;
    f8(x, s);
    "{s}\\n";
}
"""),
    ("f_u64_sum", """\
kernel su[n](in U64[n] x, out U64 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    U64[2] x = [0xFFFFFFFFFFFFFFFF, 1];
    U64 s = 0;
    su(x, s);
    "{s}\\n";
}
"""),
    ("f_wide_sum", """\
kernel ws[n](in I128[n] x, out I128 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I128[2] x = [170141183460469231731687303715884105727, 1];
    I128 s = 0;
    ws(x, s);
    "{s}\\n";
}
"""),
    ("f_wide_fold", """\
kernel wf[n](in I128[n] x, out I128 s) over [i: n] {
    reduce s = fold_checked(add, 0, x[i]);
}

void main() {
    I128[3] x = [-5, 170141183460469231731687303715884105727, 6];
    I128 s = 0;
    wf(x, s);
    "{s}\\n";
}
"""),
    ("f_fixed_sum", """\
kernel qs[n](in Q8.8[n] x, out Q8.8 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    Q8.8[2] x = [127.0, 1.0];
    Q8.8 s = 0.0;
    qs(x, s);
    "{s}\\n";
}
"""),
    ("f_global_out", """\
I32 g = 3;

kernel kf[n](in I32[n] x, out I32 s) over [i: n] {
    reduce s = fold_checked(add, 0, x[i]);
}

void main() {
    I32[2] x = [2147483647, 1];
    kf(x, g);
    "{g}\\n";
}
"""),
    ("f_q128_min_empty", """\
kernel lo[n](in Q80.48[n] x, out Q80.48 m) over [i: n] {
    if (x[i] > 1000000.0) {
        reduce m = min(x[i]);
    }
}

void main() {
    Q80.48[3] x = [1.0, 2.0, 3.0];
    Q80.48 m = 0.0;
    lo(x, m);
    "{m}\\n";
}
"""),
    ("f_wide_into_narrow", """\
kernel wn[n](in I128[n] x, out I64 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I128[2] x = [9223372036854775807, 1];
    I64 s = 0;
    wn(x, s);
    "{s}\\n";
}
"""),
)


def outcome(work: Path, exe: Path):
    fault, fuel = work / "fault.bin", work / "fuel.bin"
    for f in (fault, fuel):
        f.unlink(missing_ok=True)
    r = subprocess.run([str(exe), str(fault), str(fuel)], capture_output=True, timeout=60)
    return r.returncode, r.stdout, fault.read_bytes() if fault.exists() else b"", fuel.read_bytes()


def build(cint: Path, tc, work: Path, name: str, tag: str) -> Path:
    out = work / ("bin-" + tag)
    cmd = [str(cint), "build", "--root", str(work), "--cache", str(work / ("cache-" + tag)), "--out", str(out)]
    if tc is not None:
        cmd += ["--toolchain", str(tc)]
    r = subprocess.run(cmd + [name + ".ci"], capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        raise AssertionError(f"{tag} build failed for {name}:\n{r.stderr}")
    return out / name


def run_case(cint: Path, tc: Path, work: Path, name: str, source: str, kind: str, modules: dict | None = None):
    work.mkdir(parents=True, exist_ok=True)
    for rel, text in (modules or {}).items():
        (work / rel).write_text(text, encoding="utf-8")
    (work / (name + ".ci")).write_text(source, encoding="utf-8")
    ref = run_program(source.encode("utf-8"), name + ".ci", root=str(work))
    assert ref.kind == kind, f"{name}: the reference gives {ref.kind}, the probe was written for {kind}"
    c = outcome(work, build(cint, None, work, name, "c17"))
    qb = outcome(work, build(cint, tc, work, name, "qbe"))
    assert qb == c, f"{name}: cpu-qbe and cpu-c17 differ\nqbe: {qb!r}\nc17: {c}"
    code, stdout, fault, fuel = qb
    got_fuel = int.from_bytes(fuel[-8:], "little", signed=True)
    assert got_fuel == ref.fuel, f"{name}: fuel {got_fuel} != {ref.fuel}"
    if kind == "value":
        assert code == 0, f"{name}: unexpected exit {code}"
        assert stdout == ref.stdout, f"{name}: stdout mismatch\ngot:  {stdout!r}\nwant: {ref.stdout!r}"
    else:
        assert code == 1 and ref.record is not None, f"{name}: expected a fault exit, got {code}"
        got = decode_fault_record(fault, RUN_TIME)[1].to_expect_lines(5)
        want = ref.record.to_expect_lines(5)
        assert got == want, f"{name}: fault record mismatch\ngot:  {got}\nwant: {want}"
    return stdout


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cint", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--qbe", type=Path, help="the QBE binary (default: qbe on PATH)")
    a = p.parse_args()
    if not __debug__:
        p.error("assertions must be enabled")
    a.cint, a.out = a.cint.resolve(), a.out.resolve()
    if a.out == ROOT or ROOT in a.out.parents:
        p.error("outputs must be outside the checkout")
    a.out.mkdir(parents=True, exist_ok=False)
    tc = toolchain(a.cint, a.out, a.qbe)
    for name, source, *modules in VALID:
        run_case(a.cint, tc, a.out / name, name, source, "value", *modules)
    for name, source, *modules in FAULTS:
        run_case(a.cint, tc, a.out / name, name, source, "fault", *modules)
    for name, source in REVIEW_VALID:
        run_case(a.cint, tc, a.out / name, name, source, "value")
    for name, source in REVIEW_FAULTS:
        run_case(a.cint, tc, a.out / name, name, source, "fault")
    for name, c17_builds, source in REFUSED:
        work = a.out / name
        work.mkdir(parents=True)
        (work / (name + ".ci")).write_text(source, encoding="utf-8")
        if c17_builds:
            build(a.cint, None, work, name, "c17")
        r = subprocess.run([str(a.cint), "emit-qbe", "--root", str(work), "--toolchain", str(tc),
                            "--out", str(work / "ssa"), name + ".ci"], capture_output=True, text=True, timeout=60)
        assert r.returncode != 0 and "E_UNSUPPORTED cpu-qbe capability entry" in r.stderr, (
            f"{name}: cpu-qbe must refuse it with capability entry\n{r.stderr}")

    # The kernel's function and its dispatch, as w_func and w_kcall write them.
    emit = a.out / "ssa"
    subprocess.run([str(a.cint), "emit-qbe", "--root", str(a.out / "dispatch_args"), "--toolchain", str(tc),
                    "--out", str(emit), "dispatch_args.ci"], check=True, capture_output=True, timeout=60)
    stage = emit.parent / (emit / "MANIFEST.ref").read_text().split()[0]
    ssa = (stage / "dispatch_args.ssa").read_text(encoding="utf-8")
    heads = re.findall(r"^export function w \$\w+_k\w+\(l %ctx, :site %ks, w %kw, .*$", ssa, re.M)
    assert len(heads) == 3, f"expected three kernel functions, found {heads}"
    assert ssa.count("call $cint_rt_dispatch_fault(l %ctx, l $") == 3
    assert ssa.count("call $cint_rt_dispatch_end(l %ctx, l %kd)") == 3
    calls = host_dispatch(a.cint, tc, a.out / "host")
    nv, nf = len(VALID) + len(REVIEW_VALID), len(FAULTS) + len(REVIEW_FAULTS)
    print(f"PASS cpu-qbe kernels: {nv} value probes and {nf} fault probes agree with cint_ref "
          f"and byte for byte with cpu-c17 (stdout, fuel, fault record); {len(REFUSED)} exported kernels "
          f"refused with capability entry; {calls} host-issued dispatches through the cx wrappers and "
          f"every reflection row give the same transcripts on both")


def keys(path: Path) -> dict:
    """The lines of a toolchain file by key, in order."""
    out = {}
    for s in path.read_text().splitlines():
        k, _, v = s.partition(" ")
        out.setdefault(k, []).append(v)
    return out


def host_dispatch(cint: Path, tc: Path, work: Path) -> int:
    """compiler/tests/qbe/khost_host.c linked with khost.ci as cpu-c17 writes it and as cpu-qbe
    writes it, against the release runtime object of the toolchain beside cint: the transcripts
    of its calls (status, refusal, fuel, fault record bytes, written views and outs with their
    guards) must be equal, and the statuses the ones these calls must give."""
    src = ROOT / "compiler" / "tests" / "qbe"
    c_tc = keys(cint.parent / "cint.toolchain")
    cc, include, runtime = c_tc["cc"][0], c_tc["include"][0], c_tc["runtime_object"][0]
    flags = [f for f in c_tc.get("flag", []) if f != "-Werror"]
    q_tc = keys(tc)
    work.mkdir(parents=True)
    host_obj = work / "khost_host.o"
    subprocess.run([cc, *flags, "-I", include, "-c", str(src / "khost_host.c"), "-o", str(host_obj)],
                   check=True, capture_output=True, timeout=120)
    rows_obj = work / "khost_rows.o"
    subprocess.run([cc, *flags, "-I", include, "-c", str(src / "khost_rows.c"), "-o", str(rows_obj)],
                   check=True, capture_output=True, timeout=120)
    transcripts = {}
    rows = {}
    texts = {}
    for tag in ("c17", "qbe"):
        emit = work / ("emit-" + tag)
        cmd = [str(cint), "emit-c" if tag == "c17" else "emit-qbe", "--root", str(src), "--out", str(emit)]
        if tag == "qbe":
            cmd += ["--toolchain", str(tc)]
        subprocess.run(cmd + ["khost.ci"], check=True, capture_output=True, timeout=120)
        stage = emit.parent / (emit / "MANIFEST.ref").read_text().split()[0]
        texts[tag] = (stage / ("khost.c" if tag == "c17" else "khost.ssa")).read_text(encoding="utf-8")
        objs = []
        for f in sorted(stage.rglob("*.c" if tag == "c17" else "*.ssa")):
            o = work / (tag + "-" + f.stem + ".o")
            if tag == "c17":
                subprocess.run([cc, *flags, "-I", include, "-c", str(f), "-o", str(o)],
                               check=True, capture_output=True, timeout=120)
            else:
                s = work / (tag + "-" + f.stem + ".s")
                subprocess.run([q_tc["qbe"][0], "-t", q_tc["target"][0], "-o", str(s), str(f)],
                               check=True, capture_output=True, timeout=120)
                subprocess.run([q_tc["assembler"][0], *q_tc.get("as_flag", []), "-o", str(o), str(s)],
                               check=True, capture_output=True, timeout=120)
            objs.append(str(o))
        exe = work / ("khost-" + tag)
        subprocess.run([cc, "-o", str(exe), str(host_obj), *objs, runtime, "-ldl"],
                       check=True, capture_output=True, timeout=120)
        r = subprocess.run([str(exe)], capture_output=True, timeout=60)
        assert r.returncode == 0, f"khost-{tag}: exit {r.returncode}\n{r.stderr!r}"
        transcripts[tag] = r.stdout.decode("ascii")
        # The reflection table, row by row (khost_rows.c): kind, effect, counts, names, signature.
        rexe = work / ("khost-rows-" + tag)
        subprocess.run([cc, "-o", str(rexe), str(rows_obj), *objs, runtime, "-ldl"],
                       check=True, capture_output=True, timeout=120)
        r = subprocess.run([str(rexe)], capture_output=True, timeout=60)
        assert r.returncode == 0, f"khost-rows-{tag}: exit {r.returncode}\n{r.stderr!r}"
        rows[tag] = r.stdout.decode("ascii")
    assert transcripts["qbe"] == transcripts["c17"], (
        "host-issued dispatch differs\nqbe:\n" + transcripts["qbe"] + "\nc17:\n" + transcripts["c17"])
    assert rows["qbe"] == rows["c17"], (
        "reflection rows differ\nqbe:\n" + rows["qbe"] + "\nc17:\n" + rows["c17"])
    kernel_rows = [ln for ln in rows["qbe"].splitlines() if " kind 2 effect 1 " in ln]
    assert len(kernel_rows) == 4 and all(" sizes 0 " not in ln for ln in kernel_rows), rows["qbe"]
    lines = transcripts["qbe"].splitlines()
    status = {ln.split(" ")[0]: int(ln.split(" ")[2]) for ln in lines}
    ok, fault, refused = 0, 1, 2   # CINT_OK, CINT_FAULT, CINT_REFUSED (rt/cint_rt.h)
    want = {"kscale": ok, "kscale_overflow": fault, "kscale_shape": fault, "kscale_null_out": refused,
            "kscale_alias": fault, "kscale_stale": refused, "kscale_read_view_for_out": fault,
            "kscale_type": fault, "kscale_fuel": fault, "kscale_negative": ok, "kbump": ok,
            "kbump_up": ok, "kbump_min": ok, "khuge_fuel": fault, "kramp": ok, "kramp_overflow": fault}
    assert status == want, f"host-issued dispatch statuses {status}, want {want}"
    # The writable bits each cx wrapper passes reach no record of a host-issued dispatch on the
    # CPU (cint_rt_dispatch_alias tests the binds instead, rt/cint_rt.c), so they are compared
    # with the constant cpu-c17's wrapper passes, kernel by kernel.
    for k in ("kscale", "kbump", "khuge", "kramp"):
        c = re.findall(r"ci_\w*_%s\(ctx, \(cint_site\)\{\d+u, \d+u\}, (\d+)u," % k, texts["c17"])
        q = re.findall(r"call \$ci_\w*_%s\(l %%ctx, :site \$\w+, w (\d+)," % k, texts["qbe"])
        assert len(c) == 1 and c == q, f"{k}: writable bits {q} in the QBE wrapper, {c} in the C one"
    return len(lines)


if __name__ == "__main__":
    main()
