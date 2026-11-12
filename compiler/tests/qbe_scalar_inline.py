"""Probe inline scalar checks and cold fault blocks in compiler/back_qbe.ci (CINTC-OQ-84 item 21).

python compiler/tests/qbe_scalar_inline.py --cint /path/to/cint --out /external/dir [--qbe PATH]
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ref"))
sys.path.insert(0, str(ROOT / "tools"))
from cint_qbe_check import toolchain
from cint_ref.exec import run_program
from cint_ref.faults import RUN_TIME, decode_fault_record


VALID_PROGRAM = """\
struct Box4 {
    I32[4] items;
}

I64 id_i64(I64 x) { return x; }
U64 id_u64(U64 x) { return x; }
I32 id_i32(I32 x) { return x; }
U32 id_u32(U32 x) { return x; }
I16 id_i16(I16 x) { return x; }
U16 id_u16(U16 x) { return x; }
I8 id_i8(I8 x) { return x; }
U8 id_u8(U8 x) { return x; }

void view_probe(inout U8[_] buf, in I64[1] box) {
    buf[0] = (box[0] & 255) as U8;
    buf[1] = ((box[0] >> 8) & 255) as U8;
}

void copy_probe(inout I32[_] dst, in I32[_] src) {
    copy(dst, src);
}

I32 sum_row3[m](in I32[m, 3] grid) {
    return grid[0, 0] + grid[m - 1, 2];
}

void main() {
    I8 v_i8 = id_i8(-128) + id_i8(127);
    U8 v_u8 = id_u8(200) + id_u8(55);
    I16 v_i16 = (v_i8 as I16) + (v_u8 as I16);
    U16 v_u16 = (id_i8(127) as U16) + (v_u8 as U16);
    I32 v_i32 = (v_i16 as I32) + (v_u16 as I32);
    U32 v_u32 = (v_u16 as U32) + (id_i32(2147483647) as U32);
    I64 v_i64 = (v_i32 as I64) + (v_u32 as I64);
    U64 v_u64 = (v_i64 as U64) + (id_u32(4294967295) as U64);
    "{v_i8} {v_u8} {v_i16} {v_u16} {v_i32} {v_u32} {v_i64} {v_u64}\\n";

    I64 n0 = id_u64(9223372036854775807) as I64;
    U64 n1 = id_i64(9223372036854775807) as U64;
    I8 n2 = id_i64(-128) as I8;
    I8 n3 = id_i64(127) as I8;
    U8 n4 = id_i64(255) as U8;
    I32 n5 = id_u64(2147483647) as I32;
    U32 n6 = id_i64(4294967295) as U32;
    I8 n7 = id_u8(127) as I8;
    I16 n8 = id_u16(32767) as I16;
    I32 n9 = id_u32(2147483647) as I32;
    U8 n10 = id_i8(0) as U8;
    U64 n11 = id_i8(127) as U64;
    "{n0} {n1} {n2} {n3} {n4} {n5} {n6} {n7} {n8} {n9} {n10} {n11}\\n";

    I64 sbit_m1 = -9223372036854775807;
    I64 smin = sbit_m1 - 1;
    I64 smax = 9223372036854775807;
    I64 a0 = id_i64(smax - 5) + 5;
    I64 a1 = id_i64(smin + 5) + (-5);
    I64 s0 = id_i64(smin + 5) - 5;
    I64 s1 = id_i64(smax - 5) - (-5);
    I64 av = id_i64(smin) + id_i64(smax);
    I64 sv = id_i64(-1) - id_i64(smax);
    "{a0} {a1} {s0} {s1} {av} {sv}\\n";

    I64 m0 = id_i64(-4611686018427387904) * 2;
    I64 m1 = id_i64(4611686018427387903) * 2;
    I64 m2 = id_i64(-3074457345618258602) * 3;
    I64 m3 = id_i64(3074457345618258602) * (-3);
    I64 m4 = id_i64(1) * (-9223372036854775807 - 1);
    I64 m5 = id_i64(0) * (-9223372036854775807 - 1);
    I64 m6 = id_i64(-3074457345618258602) * (-3);
    I64 m7 = id_i64(-9223372036854775807) * (-1);
    "{m0} {m1} {m2} {m3} {m4} {m5} {m6} {m7}\\n";

    I64 g0 = -id_i64(sbit_m1);
    I8 g1 = -id_i8(-127);
    I16 g16 = -id_i16(-32767);
    I32 g32 = -id_i32(-2147483647);
    U64 g2 = -id_u64(0);
    U8 g3 = -id_u8(0);
    U64 u0 = id_u64(0xFFFFFFFFFFFFFFFA) + 5;
    U64 u1 = id_u64(5) - 5;
    U64 u2 = id_u64(0x5555555555555555) * 3;
    U32 u3 = id_u32(65535) * id_u32(65537);
    I32 i3 = id_i32(-46340) * id_i32(46340);
    I8 ni8 = (id_i8(-64) - id_i8(64)) + id_i8(127);
    I16 ni16 = id_i16(-128) * id_i16(256);
    U16 nu16 = id_u16(255) * id_u16(257);
    "{g0} {g1} {g16} {g32} {g2} {g3} {u0} {u1} {u2} {u3} {i3} {ni8} {ni16} {nu16}\\n";

    I64 sh3 = id_i64(3);
    U8 sh0 = id_u8(0);
    I64 r0 = id_i64(-17) >> 2;
    I64 r1 = id_i64(-17) >> sh3;
    I64 r2 = id_i64(smin) >> 63;
    I64 d0 = id_i64(-17) / 4;
    I64 d1 = id_i64(-17) % 4;
    I64 d2 = id_i64(-16) / 4;
    I64 d3 = id_i64(-16) % 4;
    I64 d4 = id_i64(smin) / 2;
    I64 d5 = id_i64(smin) % 2;
    I64 d6 = id_i64(-1) / 1;
    I64 d7 = id_i64(-1) % 1;
    U64 d8 = id_u64(0x8000000000000005) / 0x8000000000000000;
    U64 d9 = id_u64(0x8000000000000005) % 0x8000000000000000;
    I8 nd0 = id_i8(-128) / 2;
    I8 nd1 = id_i8(-17) / 4;
    I8 nd2 = id_i8(-17) % 4;
    I16 nd3 = id_i16(-32767) / 8;
    I16 nd4 = id_i16(-32767) % 8;
    I32 nd5 = id_i32(-2147483647) / 16;
    I32 nd6 = id_i32(-2147483647) % 16;
    I8 l0 = id_i8(-64) << 1;
    I8 l1 = id_i8(-64) << id_i32(1);
    U8 l2 = id_u8(255) <<% 3;
    U8 l3 = id_u8(255) <<% id_u64(3);
    I64 l4 = id_i64(smin) << sh0;
    I64 l5 = id_i64(0x3FFFFFFFFFFFFFFF) << 1;
    I64 l6 = id_i64(-0x4000000000000000) << id_i64(1);
    U64 l7 = id_u64(0x7FFFFFFFFFFFFFFF) << 1;
    "{r0} {r1} {r2} {d0} {d1} {d2} {d3} {d4} {d5} {d6} {d7} {d8} {d9} {nd0} {nd1} {nd2} {nd3} {nd4} {nd5} {nd6} {l0} {l1} {l2} {l3} {l4} {l5} {l6} {l7}\\n";

    U64 b0 = uabs(id_i64(smin));
    U8 b1 = uabs(id_i8(-128));
    U32 b2 = uabs(id_i32(0));
    I64 b3 = min(id_i64(smin), id_i64(smax));
    I64 b4 = max(id_i64(smin), id_i64(smax));
    U64 b5 = min(id_u64(0), id_u64(0xFFFFFFFFFFFFFFFF));
    U8 b6 = max(id_u8(12), id_u8(250));
    "{b0} {b1} {b2} {b3} {b4} {b5} {b6}\\n";

    U8[4] bytes = [9, 8, 7, 6];
    I64[1] box = [0x1234];
    view_probe(bytes[1..3], box);
    I32[3] src = [10, 20, 30];
    I32[3] dst = [0, 0, 0];
    copy_probe(dst, src);
    Box4 bx;
    bx.items[0] = 11;
    bx.items[3] = 44;
    I32[2, 3] mat;
    mat[0, 0] = 5;
    mat[1, 2] = 19;
    I32 ms = sum_row3(mat);
    "{bytes[0]} {bytes[1]} {bytes[2]} {bytes[3]} {dst[0]} {dst[1]} {dst[2]} {bx.items[0]} {bx.items[3]} {ms}\\n";
}
"""


FAULT_CASES = (
    ("narrow_s2u_i64_u32", "I64 f(I64 x) { return (x as U32) as I64; }\nvoid main() { \"{f(-1)}\\n\"; }\n"),
    ("narrow_s2u_i8_u64", "U64 f(I8 x) { return x as U64; }\nvoid main() { \"{f(-1)}\\n\"; }\n"),
    ("narrow_s2u_i64_u8_hi", "U8 f(I64 x) { return x as U8; }\nvoid main() { \"{f(256)}\\n\"; }\n"),
    ("narrow_u2s_u64_i64", "I64 f(U64 x) { return x as I64; }\nvoid main() { \"{f(0x8000000000000000)}\\n\"; }\n"),
    ("narrow_u2s_u8_i8", "I8 f(U8 x) { return x as I8; }\nvoid main() { \"{f(128)}\\n\"; }\n"),
    ("narrow_u2s_u16_i16", "I16 f(U16 x) { return x as I16; }\nvoid main() { \"{f(32768)}\\n\"; }\n"),
    ("narrow_u2s_u32_i32", "I32 f(U32 x) { return x as I32; }\nvoid main() { \"{f(2147483648)}\\n\"; }\n"),
    ("narrow_u2u_u64_u8", "U8 f(U64 x) { return x as U8; }\nvoid main() { \"{f(256)}\\n\"; }\n"),
    ("narrow_s2s_lo", "I8 f(I64 x) { return x as I8; }\nvoid main() { \"{f(-129)}\\n\"; }\n"),
    ("narrow_s2s_hi", "I8 f(I64 x) { return x as I8; }\nvoid main() { \"{f(128)}\\n\"; }\n"),
    ("narrow_s2s_i16", "I16 f(I64 x) { return x as I16; }\nvoid main() { \"{f(-32769)}\\n\"; }\n"),
    ("narrow_s2s_i32", "I32 f(I64 x) { return x as I32; }\nvoid main() { \"{f(2147483648)}\\n\"; }\n"),
    ("add_const_hi", "I64 f(I64 x) { return x + 1; }\nvoid main() { \"{f(9223372036854775807)}\\n\"; }\n"),
    ("add_const_lo", "I64 f(I64 x) { return x + (-1); }\nvoid main() { \"{f(-9223372036854775807 - 1)}\\n\"; }\n"),
    ("sub_const_lo", "I64 f(I64 x) { return x - 1; }\nvoid main() { \"{f(-9223372036854775807 - 1)}\\n\"; }\n"),
    ("sub_const_hi", "I64 f(I64 x) { return x - (-1); }\nvoid main() { \"{f(9223372036854775807)}\\n\"; }\n"),
    ("add_var_ovf", "I64 f(I64 a, I64 b) { return a + b; }\nvoid main() { \"{f(0x6000000000000000, 0x4000000000000000)}\\n\"; }\n"),
    ("sub_var_ovf", "I64 f(I64 a, I64 b) { return a - b; }\nvoid main() { \"{f(-0x6000000000000000, 0x4000000000000000)}\\n\"; }\n"),
    ("mul_const_pos_lo", "I64 f(I64 x) { return x * 3; }\nvoid main() { \"{f(-3074457345618258603)}\\n\"; }\n"),
    ("mul_const_pos_hi", "I64 f(I64 x) { return x * 3; }\nvoid main() { \"{f(3074457345618258603)}\\n\"; }\n"),
    ("mul_const_neg_hi", "I64 f(I64 x) { return x * (-3); }\nvoid main() { \"{f(-3074457345618258603)}\\n\"; }\n"),
    ("mul_const_neg_lo", "I64 f(I64 x) { return x * (-3); }\nvoid main() { \"{f(3074457345618258603)}\\n\"; }\n"),
    ("mul_const_neg1", "I64 f(I64 x) { return x * (-1); }\nvoid main() { \"{f(-9223372036854775807 - 1)}\\n\"; }\n"),
    ("mul_neg_min", "I64 f(I64 x) { return x * (-9223372036854775807 - 1); }\nvoid main() { \"{f(-1)}\\n\"; }\n"),
    ("add_u64_ovf", "U64 f(U64 a, U64 b) { return a + b; }\nvoid main() { \"{f(0xFFFFFFFFFFFFFFFF, 1)}\\n\"; }\n"),
    ("sub_u64_ovf", "U64 f(U64 a, U64 b) { return a - b; }\nvoid main() { \"{f(5, 6)}\\n\"; }\n"),
    ("mul_u64_const_ovf", "U64 f(U64 x) { return x * 3; }\nvoid main() { \"{f(0x5555555555555556)}\\n\"; }\n"),
    ("add_i8_hi", "I8 f(I8 a, I8 b) { return a + b; }\nvoid main() { \"{f(127, 1)}\\n\"; }\n"),
    ("sub_i8_lo", "I8 f(I8 a, I8 b) { return a - b; }\nvoid main() { \"{f(-128, 1)}\\n\"; }\n"),
    ("mul_i8_ovf", "I8 f(I8 a, I8 b) { return a * b; }\nvoid main() { \"{f(-128, -1)}\\n\"; }\n"),
    ("add_i16_hi", "I16 f(I16 a, I16 b) { return a + b; }\nvoid main() { \"{f(32767, 1)}\\n\"; }\n"),
    ("sub_i16_lo", "I16 f(I16 a, I16 b) { return a - b; }\nvoid main() { \"{f(-32768, 1)}\\n\"; }\n"),
    ("mul_i16_ovf", "I16 f(I16 a, I16 b) { return a * b; }\nvoid main() { \"{f(256, 128)}\\n\"; }\n"),
    ("add_i32_hi", "I32 f(I32 a, I32 b) { return a + b; }\nvoid main() { \"{f(2147483647, 1)}\\n\"; }\n"),
    ("sub_i32_lo", "I32 f(I32 a, I32 b) { return a - b; }\nvoid main() { \"{f(-2147483647 - 1, 1)}\\n\"; }\n"),
    ("mul_i32_ovf", "I32 f(I32 a, I32 b) { return a * b; }\nvoid main() { \"{f(46341, 46341)}\\n\"; }\n"),
    ("add_u8_ovf", "U8 f(U8 a, U8 b) { return a + b; }\nvoid main() { \"{f(255, 1)}\\n\"; }\n"),
    ("sub_u8_ovf", "U8 f(U8 a, U8 b) { return a - b; }\nvoid main() { \"{f(0, 1)}\\n\"; }\n"),
    ("mul_u8_ovf", "U8 f(U8 a, U8 b) { return a * b; }\nvoid main() { \"{f(16, 16)}\\n\"; }\n"),
    ("add_u16_ovf", "U16 f(U16 a, U16 b) { return a + b; }\nvoid main() { \"{f(65535, 1)}\\n\"; }\n"),
    ("sub_u16_ovf", "U16 f(U16 a, U16 b) { return a - b; }\nvoid main() { \"{f(0, 1)}\\n\"; }\n"),
    ("mul_u16_ovf", "U16 f(U16 a, U16 b) { return a * b; }\nvoid main() { \"{f(256, 256)}\\n\"; }\n"),
    ("add_u32_ovf", "U32 f(U32 a, U32 b) { return a + b; }\nvoid main() { \"{f(4294967295, 1)}\\n\"; }\n"),
    ("sub_u32_ovf", "U32 f(U32 a, U32 b) { return a - b; }\nvoid main() { \"{f(0, 1)}\\n\"; }\n"),
    ("mul_u32_ovf", "U32 f(U32 a, U32 b) { return a * b; }\nvoid main() { \"{f(65536, 65536)}\\n\"; }\n"),
    ("neg_smin", "I64 f(I64 x) { return -x; }\nvoid main() { \"{f(-9223372036854775807 - 1)}\\n\"; }\n"),
    ("neg_i8_min", "I8 f(I8 x) { return -x; }\nvoid main() { \"{f(-128)}\\n\"; }\n"),
    ("neg_i16_min", "I16 f(I16 x) { return -x; }\nvoid main() { \"{f(-32768)}\\n\"; }\n"),
    ("neg_i32_min", "I32 f(I32 x) { return -x; }\nvoid main() { \"{f(-2147483647 - 1)}\\n\"; }\n"),
    ("neg_u8_nz", "U8 f(U8 x) { return -x; }\nvoid main() { \"{f(1)}\\n\"; }\n"),
    ("neg_u64_nz", "U64 f(U64 x) { return -x; }\nvoid main() { \"{f(1)}\\n\"; }\n"),
    ("shl_count_neg", "I64 f(I64 a, I8 k) { return a << k; }\nvoid main() { \"{f(1, -1)}\\n\"; }\n"),
    ("shl_var_ovf", "I8 f(I8 a, U8 k) { return a << k; }\nvoid main() { \"{f(64, 1)}\\n\"; }\n"),
    ("shl_const_i8_ovf", "I8 f(I8 a) { return a << 1; }\nvoid main() { \"{f(64)}\\n\"; }\n"),
    ("shl_const_u8_ovf", "U8 f(U8 a) { return a << 1; }\nvoid main() { \"{f(128)}\\n\"; }\n"),
    ("shl_const_i64_hi", "I64 f(I64 a) { return a << 1; }\nvoid main() { \"{f(0x4000000000000000)}\\n\"; }\n"),
    ("shl_const_i64_lo", "I64 f(I64 a) { return a << 1; }\nvoid main() { \"{f(-0x4000000000000001)}\\n\"; }\n"),
    ("shl_var_i64_hi", "I64 f(I64 a, I64 k) { return a << k; }\nvoid main() { \"{f(0x4000000000000000, 1)}\\n\"; }\n"),
    ("shl_var_u64_ovf", "U64 f(U64 a, U8 k) { return a << k; }\nvoid main() { \"{f(0x8000000000000000, 1)}\\n\"; }\n"),
    ("shr_count_hi", "U32 f(U32 a, U64 k) { return a >> k; }\nvoid main() { \"{f(1, 32)}\\n\"; }\n"),
    ("bounds_view", "U8 f(in U8[_] s, I64 k) { return s[k]; }\nvoid main() { U8[2] a = [1, 2]; \"{f(a, 2)}\\n\"; }\n"),
    ("bounds_rec_field", "struct S { I32[4] v; }\nI32 f(S s, I64 k) { return s.v[k]; }\nvoid main() { S s; \"{f(s, 4)}\\n\"; }\n"),
    ("shape_copy", "void f(inout I32[_] d, in I32[_] s) { copy(d, s); }\nvoid main() { I32[2] d = [0, 0]; I32[3] s = [1, 2, 3]; f(d, s); }\n"),
    ("shape_bind_1d", "I32 f(in I32[3] s) { return s[0]; }\nvoid g(in I32[_] s) { \"{f(s)}\\n\"; }\nvoid main() { I32[2] a = [1, 2]; g(a); }\n"),
    ("shape_bind_2d_dim", "I32 f[m](in I32[m, 3] s) { return s[0, 0]; }\nvoid g[m, n](in I32[m, n] s) { \"{f(s)}\\n\"; }\nvoid main() { I32[2, 4] a; g(a); }\n"),
)


# One probe at each limit of each inline test (cint board 20261111T185930Z finding 5): the first
# value past the bound must leave the canonical fault record, and LIMIT_PROGRAM holds the last
# value inside it. A limit FAULT_CASES already probes with the same program is not repeated.
I64_MIN = "-9223372036854775807 - 1"
# Written in an expression, -9223372036854775807 - 1 is a checked subtraction at run time, so
# the operand reaching the emitter is not a constant node; a named constant is one.
MIN64 = "const I64 MIN64 = -9223372036854775807 - 1;\n"


def _main(decl, call):
    return decl + '\nvoid main() { "{' + call + '}\\n"; }\n'


def _as(src, dst, value):
    return _main(f"{dst} f({src} x) {{ return x as {dst}; }}", f"f({value})")


def _bin(ty, op, a, b, count=None):
    return _main(f"{ty} f({ty} a, {count or ty} b) {{ return a {op} b; }}", f"f({a}, {b})")


def _un(ty, expr, value):
    return _main(f"{ty} f({ty} x) {{ return {expr}; }}", f"f({value})")


_BIND = "I64 g[n](in I64[n] a, in I64[n] b) { return a[0] + b[0]; }\n" \
        "I64 f[n, m](in I64[n] a, in I64[m] b) { return g(a, b); }\n"
_BIND2 = "I64 g[h, w](in I64[h, w] a, in I64[h, w] b) { return a[0, 0] + b[0, 0]; }\n" \
         "I64 f(in I64[_, _] a, in I64[_, _] b) { return g(a, b); }\n"
_REC = "struct R { I64 n; I32[3] xs; }\n"

LIMIT_FAULTS = (
    # Conversions: signed to unsigned, unsigned to signed, unsigned and signed narrowing.
    ("as_i8_u8_neg", _as("I8", "U8", "-1")),
    ("as_i64_u64_neg", _as("I64", "U64", "-1")),
    ("as_i32_u16_hi", _as("I32", "U16", "65536")),
    ("as_i64_u32_hi", _as("I64", "U32", "4294967296")),
    ("as_u64_i32_hi", _as("U64", "I32", "2147483648")),
    ("as_u32_u16_hi", _as("U32", "U16", "65536")),
    ("as_u64_u32_hi", _as("U64", "U32", "4294967296")),
    ("as_i16_i8_lo", _as("I16", "I8", "-129")),
    ("as_i32_i16_hi", _as("I32", "I16", "32768")),
    ("as_i64_i32_lo", _as("I64", "I32", "-2147483649")),
    # Narrow add, sub and mul, each side of each signed range and the top of each unsigned one.
    ("add_i8_lo", _bin("I8", "+", "-128", "-1")),
    ("sub_i8_hi", _bin("I8", "-", "127", "-1")),
    ("mul_i8_hi", _bin("I8", "*", "16", "8")),
    ("mul_i8_lo", _bin("I8", "*", "-43", "3")),
    ("mul_i32_lo", _bin("I32", "*", "-65536", "32769")),
    ("sub_u16_lo", _bin("U16", "-", "7", "8")),
    ("add_i8_const", _un("I8", "x + 1", "127")),
    ("mul_u8_const", _un("U8", "x * 2", "128")),
    # U64: carry, borrow, constant multiply on either side, and the helper's variable multiply.
    ("add_u64_const", _un("U64", "x + 1", "0xFFFFFFFFFFFFFFFF")),
    ("add_u64_const_left", _un("U64", "1 + x", "0xFFFFFFFFFFFFFFFF")),
    ("sub_u64_borrow", _bin("U64", "-", "0", "1")),
    ("sub_u64_high", _bin("U64", "-", "1", "0x8000000000000000")),
    ("sub_u64_const", _un("U64", "x - 1", "0")),
    ("mul_u64_const_left", _un("U64", "3 * x", "0x5555555555555556")),
    ("mul_u64_const_high", _un("U64", "x * 0x8000000000000000", "2")),
    ("mul_u64_var", _bin("U64", "*", "0x100000000", "0x100000000")),
    # I64: constant multiply past each bound for both signs of the constant, the constant on the
    # left, the other side of variable add and sub, and I64.min as the constant.
    ("mul_const_left_hi", _un("I64", "3 * x", "3074457345618258603")),
    ("mul_const_m2_lo", _un("I64", "x * (-2)", "4611686018427387905")),
    ("mul_const_m2_hi", _un("I64", "x * (-2)", "-4611686018427387904")),
    ("mul_const_min_2", MIN64 + _un("I64", "x * MIN64", "2")),
    ("mul_const_min_m1", MIN64 + _un("I64", "MIN64 * x", "-1")),
    ("mul_i64_var", _bin("I64", "*", "0x100000000", "0x80000000")),
    ("add_var_lo", _bin("I64", "+", I64_MIN, "-1")),
    ("sub_var_hi", _bin("I64", "-", "9223372036854775807", "-1")),
    ("add_const_left", _un("I64", "1 + x", "9223372036854775807")),
    ("sub_const_left", _un("I64", "(-2) - x", "9223372036854775807")),
    ("add_const_min", MIN64 + _un("I64", "x + MIN64", "-1")),
    ("add_const_min_left", MIN64 + _un("I64", "MIN64 + x", "-1")),
    ("sub_const_min", MIN64 + _un("I64", "x - MIN64", "0")),
    # Shifts: a lost bit with a constant and a variable count, the count at the width, a count
    # whose low 32 bits are in range, a negative count, and a constant count at the width.
    ("shl_const_i8", _un("I8", "x << 1", "64")),
    ("shl_const_i8_neg", _un("I8", "x << 1", "-65")),
    ("shl_const_u8", _un("U8", "x << 1", "128")),
    ("shl_const_i32", _un("I32", "x << 4", "0x8000000")),
    ("shl_const_i64", _un("I64", "x << 1", "0x4000000000000000")),
    ("shl_const_i64_neg", _un("I64", "x << 1", "-0x4000000000000001")),
    ("shl_const_u64", _un("U64", "x << 1", "0x8000000000000000")),
    ("shl_var_i64", _bin("I64", "<<", "0x4000000000000000", "1", "U8")),
    ("shl_var_i64_neg", _bin("I64", "<<", "-0x4000000000000001", "1", "I64")),
    ("shl_var_u64", _bin("U64", "<<", "0x8000000000000000", "1", "U8")),
    ("shl_var_i32", _bin("I32", "<<", "0x40000000", "1", "I32")),
    ("shl_var_u16", _bin("U16", "<<", "0x8000", "1", "U16")),
    ("shl_count_i8_width", _bin("I8", "<<", "0", "8", "U8")),
    ("shl_count_i64_width", _bin("I64", "<<", "0", "64", "I64")),
    ("shl_count_u32_width", _bin("U32", "<<", "1", "32", "U32")),
    ("shl_count_low_bits", _bin("I64", "<<", "1", "0x100000001", "U64")),
    ("shl_wrap_count_u8", _bin("U8", "<<%", "1", "8", "U8")),
    ("shl_wrap_count_i64", _bin("I64", "<<%", "1", "64", "U64")),
    ("shl_wrap_count_neg", _bin("I32", "<<%", "1", "-1", "I16")),
    ("shr_count_i64_width", _bin("I64", ">>", "-1", "64", "I64")),
    ("shr_count_i8_width", _bin("I8", ">>", "-1", "8", "U8")),
    ("shr_count_neg", _bin("U64", ">>", "1", "-1", "I64")),
    ("shr_count_low_bits", _bin("U32", ">>", "8", "0x100000001", "U64")),
    ("shl_const_count_width", _un("I8", "x << 8", "0")),
    ("shr_const_count_width", _un("U8", "x >> 8", "1")),
    # Negation of each narrow minimum and of a nonzero unsigned value.
    ("neg_u64_high", _un("U64", "-x", "0x8000000000000000")),
    ("neg_u32_nz", _un("U32", "-x", "1")),
    # Index checks that must stay: a constant index at the extent of a fixed array and of a
    # record's array field, and a constant index into a view, a sized parameter and rank 2.
    ("bounds_fixed_const", _main("I64 f() { I64[3] a = [1, 2, 3]; return a[3]; }", "f()")),
    ("bounds_field_const", _main(_REC + "I32 f() { R r; return r.xs[3]; }", "f()")),
    ("bounds_fixed_var_hi", _main("I64 f(I64 k) { I64[3] a = [1, 2, 3]; return a[k]; }", "f(3)")),
    ("bounds_fixed_var_neg", _main("I64 f(I64 k) { I64[3] a = [1, 2, 3]; return a[k]; }", "f(-1)")),
    ("bounds_field_var", _main(_REC + "I32 f(I64 k) { R r; return r.xs[k]; }", "f(3)")),
    ("bounds_view_const", "U8 f(in U8[_] s) { return s[2]; }\nvoid main() { U8[2] a = [1, 2]; \"{f(a)}\\n\"; }\n"),
    ("bounds_size_const", "I64 f[n](in I64[n] s) { return s[3]; }\nvoid main() { I64[3] a; \"{f(a)}\\n\"; }\n"),
    ("bounds_size_local", "I64 f[n](in I64[n] s) { I64[n] t; return t[2]; }\nvoid main() { I64[2] a; \"{f(a)}\\n\"; }\n"),
    ("bounds_rank2_const", "I64 f(in I64[_, _] m) { return m[1, 3]; }\nvoid main() { I64[2, 3] a; \"{f(a)}\\n\"; }\n"),
    ("bounds_rank2_fixed", _main("I64 f(I64 k) { I64[2, 3] a; return a[1, k]; }", "f(3)")),
    # A descriptor view's type holds its rank where an array's holds its extent, so a constant
    # index below the rank must still be checked against the run-time extent.
    ("bounds_rank2_dim0", "I64 f(in I64[_, _] m) { return m[1, 0]; }\nvoid main() { I64[1, 3] a; \"{f(a)}\\n\"; }\n"),
    ("bounds_local_empty", "I64 f[n](in I64[n] s) { I64[n] t; return t[0]; }\n"
                           "void main() { I64[0] a; \"{f(a)}\\n\"; }\n"),
    # Shape checks: copy.shape and bind.shape with the actual extent on either side of the
    # expected one, a fixed extent, and each dimension of rank 2.
    ("shape_copy_long", "void f(inout I32[_] d, in I32[_] s) { copy(d, s); }\n"
                        "void main() { I32[3] d = [0, 0, 0]; I32[2] s = [1, 2]; f(d, s); }\n"),
    ("shape_bind_short", _BIND + "void main() { I64[4] a; I64[3] b; \"{f(a, b)}\\n\"; }\n"),
    ("shape_bind_long", _BIND + "void main() { I64[3] a; I64[4] b; \"{f(a, b)}\\n\"; }\n"),
    ("shape_bind_fixed", "I64 g(in I64[3] a) { return a[2]; }\nI64 f(in I64[_] a) { return g(a); }\n"
                         "void main() { I64[4] a; \"{f(a)}\\n\"; }\n"),
    ("shape_bind_dim1", _BIND2 + "void main() { I64[2, 3] a; I64[2, 4] b; \"{f(a, b)}\\n\"; }\n"),
    ("shape_bind_dim0", _BIND2 + "void main() { I64[3, 3] a; I64[2, 3] b; \"{f(a, b)}\\n\"; }\n"),
)


# The last value inside each bound, and the values the branchless forms, the folded element
# addresses and the elided index checks must produce.
LIMIT_PROGRAM = """\
I64 id_i64(I64 x) { return x; }
U64 id_u64(U64 x) { return x; }
I32 id_i32(I32 x) { return x; }
U32 id_u32(U32 x) { return x; }
I16 id_i16(I16 x) { return x; }
U16 id_u16(U16 x) { return x; }
I8 id_i8(I8 x) { return x; }
U8 id_u8(U8 x) { return x; }

struct Rec { I64 n; I32[3] xs; }
const I64 MIN64 = -9223372036854775807 - 1;

I32 first_i32(in I32[_] v) { return v[0]; }
I32 at_i32(in I32[_] v, I64 k) { return v[k]; }
U8 first_u8(in U8[_] v) { return v[0]; }
U8 at_u8(in U8[_] v, I64 k) { return v[k]; }
I64 first_i64(in I64[_] v) { return v[0]; }
I64 last_sized[n](in I64[n] v) { return v[n - 1]; }
I64 corner(in I64[_, _] m) { return m[1, 2] + m[0, 0]; }
I64 same[n](in I64[n] a, in I64[n] b) { return a[0] + b[n - 1]; }

void main() {
    U8 c0 = id_i64(255) as U8;
    U8 c1 = id_i8(127) as U8;
    U64 c2 = id_i8(0) as U64;
    U16 c3 = id_i32(65535) as U16;
    I8 c4 = id_u8(127) as I8;
    I16 c5 = id_u16(32767) as I16;
    I32 c6 = id_u32(2147483647) as I32;
    U8 c7 = id_u64(255) as U8;
    U16 c8 = id_u32(65535) as U16;
    U32 c9 = id_u64(4294967295) as U32;
    I8 c10 = id_i16(-128) as I8;
    I16 c11 = id_i64(-32768) as I16;
    I16 c12 = id_i64(32767) as I16;
    I32 c13 = id_i64(2147483647) as I32;
    I32 c14 = id_i64(-2147483647 - 1) as I32;
    "{c0} {c1} {c2} {c3} {c4} {c5} {c6} {c7} {c8} {c9} {c10} {c11} {c12} {c13} {c14}\\n";

    I8 a0 = id_i8(126) + id_i8(1);
    I8 a1 = id_i8(-127) + id_i8(-1);
    I8 a2 = id_i8(-127) - id_i8(1);
    I8 a3 = id_i8(126) - id_i8(-1);
    I8 a4 = id_i8(64) * id_i8(-2);
    I8 a5 = id_i8(-127) * id_i8(-1);
    I16 a6 = id_i16(32766) + id_i16(1);
    I16 a7 = id_i16(-32767) - id_i16(1);
    I16 a8 = id_i16(128) * id_i16(-256);
    I32 a9 = id_i32(2147483646) + id_i32(1);
    I32 a10 = id_i32(-2147483647) - id_i32(1);
    I32 a11 = id_i32(-65536) * id_i32(32768);
    U8 a12 = id_u8(254) + id_u8(1);
    U8 a13 = id_u8(5) - id_u8(5);
    U8 a14 = id_u8(15) * id_u8(17);
    U16 a15 = id_u16(65534) + id_u16(1);
    U16 a16 = id_u16(7) - id_u16(7);
    U16 a17 = id_u16(255) * id_u16(257);
    U32 a18 = id_u32(4294967294) + id_u32(1);
    U32 a19 = id_u32(1) - id_u32(1);
    "{a0} {a1} {a2} {a3} {a4} {a5} {a6} {a7} {a8} {a9} {a10} {a11} {a12} {a13} {a14} {a15} {a16} {a17} {a18} {a19}\\n";

    U64 u0 = id_u64(0x7FFFFFFFFFFFFFFF) + id_u64(1);
    U64 u1 = id_u64(0xFFFFFFFFFFFFFFFE) + 1;
    U64 u2 = 1 + id_u64(0xFFFFFFFFFFFFFFFE);
    U64 u3 = id_u64(0x8000000000000000) - id_u64(1);
    U64 u4 = id_u64(0xFFFFFFFFFFFFFFFF) - id_u64(0xFFFFFFFFFFFFFFFF);
    U64 u5 = id_u64(0xFFFFFFFFFFFFFFFF) + id_u64(0);
    U64 u6 = 3 * id_u64(0x5555555555555555);
    U64 u7 = id_u64(1) * 0x8000000000000000;
    U64 u8 = id_u64(0xFFFFFFFFFFFFFFFF) * 1;
    U64 u9 = id_u64(0xFFFFFFFFFFFFFFFF) * 0;
    U64 u10 = id_u64(0xFFFFFFFF) * id_u64(0x100000001);
    "{u0} {u1} {u2} {u3} {u4} {u5} {u6} {u7} {u8} {u9} {u10}\\n";

    I64 smax = 9223372036854775807;
    I64 smin = -9223372036854775807 - 1;
    I64 m0 = id_i64(4611686018427387904) * (-2);
    I64 m1 = id_i64(-4611686018427387903) * (-2);
    I64 m2 = id_i64(smin + 1) * (-1);
    I64 m3 = 3 * id_i64(3074457345618258602);
    I64 m4 = (-3) * id_i64(-3074457345618258602);
    I64 m5 = id_i64(-3037000499) * id_i64(3037000499);
    I64 s0 = 1 + id_i64(smax - 1);
    I64 s1 = (-2) - id_i64(smax - 1);
    I64 s2 = id_i64(0) + MIN64;
    I64 s3 = id_i64(-1) - MIN64;
    I64 s4 = id_i64(smin) + id_i64(0);
    I64 s5 = id_i64(smax) - id_i64(0);
    I64 s6 = id_i64(smin) - id_i64(-1);
    I64 s7 = id_i64(smax) + id_i64(smin);
    "{m0} {m1} {m2} {m3} {m4} {m5} {s0} {s1} {s2} {s3} {s4} {s5} {s6} {s7}\\n";

    U8 k7 = id_u8(7);
    I64 k63 = id_i64(63);
    I8 h0 = id_i8(63) << 1;
    U8 h1 = id_u8(127) << 1;
    I64 h2 = id_i64(0x3FFFFFFFFFFFFFFF) << 1;
    I64 h3 = id_i64(-0x4000000000000000) << 1;
    U64 h4 = id_u64(0x7FFFFFFFFFFFFFFF) << 1;
    I8 h5 = id_i8(-1) << 7;
    I8 h6 = id_i8(-1) << k7;
    I8 h7 = id_i8(1) << id_i8(6);
    I64 h8 = id_i64(-1) << k63;
    U64 h9 = id_u64(1) << k63;
    I64 h10 = id_i64(-0x4000000000000000) << id_u8(1);
    U8 h11 = id_u8(255) <<% k7;
    I64 h12 = id_i64(3) <<% k63;
    I8 h13 = id_i8(-128) >> k7;
    U32 h14 = id_u32(0x80000000) >> id_u32(31);
    I64 h15 = id_i64(smin) >> k63;
    U64 h16 = id_u64(0x8000000000000000) >> 63;
    I16 h17 = id_i16(-32768) >> 15;
    "{h0} {h1} {h2} {h3} {h4} {h5} {h6} {h7} {h8} {h9} {h10} {h11} {h12} {h13} {h14} {h15} {h16} {h17}\\n";

    I8 d0 = id_i8(-17) / 4;
    I8 d1 = id_i8(-17) % 4;
    I8 d2 = id_i8(-128) / 64;
    I8 d3 = id_i8(-128) % 64;
    I32 d4 = id_i32(-7) / 2;
    I32 d5 = id_i32(-7) % 2;
    I32 d6 = id_i32(-2147483647 - 1) / 0x40000000;
    U8 d7 = id_u8(255) / 16;
    U8 d8 = id_u8(255) % 16;
    U32 d9 = id_u32(0xFFFFFFFF) / 0x80000000;
    U32 d10 = id_u32(0xFFFFFFFF) % 0x80000000;
    I64 d11 = id_i64(5) / MIN64;
    I64 d12 = id_i64(5) % MIN64;
    I64 d13 = id_i64(-5) / MIN64;
    I64 d14 = id_i64(-5) % MIN64;
    I64 d15 = id_i64(smin) / 0x4000000000000000;
    I64 d16 = id_i64(-1) % 0x4000000000000000;
    I64 d17 = id_i64(1) * MIN64;
    I64 d18 = MIN64 * id_i64(0);
    I64 d19 = id_i64(smin) >> 62;
    "{d0} {d1} {d2} {d3} {d4} {d5} {d6} {d7} {d8} {d9} {d10} {d11} {d12} {d13} {d14} {d15} {d16} {d17} {d18} {d19}\\n";

    I16 g0 = -id_i16(-32767);
    I32 g1 = -id_i32(-2147483647);
    I8 g2 = -id_i8(127);
    U32 g3 = -id_u32(0);
    U16 b0 = uabs(id_i16(-32768));
    U32 b1 = uabs(id_i32(-2147483647 - 1));
    U64 b2 = uabs(id_i64(5));
    U64 b3 = max(id_u64(1), id_u64(0x8000000000000000));
    U64 b4 = min(id_u64(0x8000000000000000), id_u64(0x7FFFFFFFFFFFFFFF));
    I8 b5 = min(id_i8(-1), id_i8(1));
    I8 b6 = max(id_i8(-1), id_i8(1));
    I32 b7 = max(id_i32(-2147483647 - 1), id_i32(2147483647));
    U32 b8 = min(id_u32(4294967295), id_u32(0));
    I64 b9 = max(id_i64(-1), id_i64(0));
    I64 b10 = min(id_i64(7), id_i64(7));
    U64 b11 = uabs(id_i64(-5));
    U8 b12 = uabs(id_i8(-1));
    U32 b13 = uabs(id_i32(-2147483647));
    "{g0} {g1} {g2} {g3} {b0} {b1} {b2} {b3} {b4} {b5} {b6} {b7} {b8} {b9} {b10} {b11} {b12} {b13}\\n";

    I64[3] fixed = [11, 22, 33];
    Rec r;
    r.xs[0] = 4;
    r.xs[2] = 6;
    I64 e0 = fixed[2] + fixed[0] + (r.xs[2] as I64) + (r.xs[0] as I64);
    I64 e1 = fixed[id_i64(1)];
    I32[5] words = [100, 200, 300, 400, 500];
    U8[5] bytes = [1, 2, 3, 4, 5];
    I64[2, 3] grid = [[1, 2, 3], [4, 5, 6]];
    I32 e2 = first_i32(words[2..4]);
    I32 e3 = at_i32(words[2..5], 2);
    I32 e4 = first_i32(words[4..=0 by -2]);
    I32 e5 = at_i32(words[4..=0 by -2], 2);
    U8 e6 = first_u8(bytes[2..4]);
    U8 e7 = at_u8(bytes[2..5], 2);
    U8 e8 = first_u8(bytes[3..=0 by -3]);
    U8 e9 = at_u8(bytes[4..=0 by -2], 1);
    I64 e10 = first_i64(fixed[1..3]);
    I64 e11 = last_sized(fixed);
    I64 e12 = corner(grid);
    I64 e13 = same(fixed, fixed[0..3]);
    "{e0} {e1} {e2} {e3} {e4} {e5} {e6} {e7} {e8} {e9} {e10} {e11} {e12} {e13}\\n";
}
"""


# plain_icheck: the index call is gone only where a constant index meets a static extent.
ELIDE_PROGRAM = """\
struct Rec { I64 n; I32[3] xs; }
I64 fixedconst() { I64[3] a = [1, 2, 3]; Rec r; r.xs[2] = 5; return a[2] + (r.xs[2] as I64); }
I64 fixedvar(I64 k) { I64[3] a = [1, 2, 3]; return a[k]; }
U8 viewconst(in U8[_] s) { return s[1]; }
I64 sizedconst[n](in I64[n] s) { return s[1]; }
void main() {
    U8[2] b = [1, 2];
    I64[2] c = [3, 4];
    "{fixedconst()} {fixedvar(2)} {viewconst(b)} {sizedconst(c)}\\n";
}
"""


def function_ssa(ssa: str, name: str) -> str:
    """The QBE text of the function whose symbol ends in _<name>."""
    m = re.search(r"^export function w \$\w+_%s\(.*?^}" % name, ssa, re.M | re.S)
    assert m, f"no function {name} in the emitted module"
    return m.group(0)


def run_case(cint: Path, tc: Path, work: Path, name: str, source: str, kind: str = ""):
    work.mkdir(parents=True, exist_ok=True)
    src = work / (name + ".ci")
    src.write_text(source, encoding="utf-8")
    ref = run_program(source.encode("utf-8"), name + ".ci", root=str(work))
    assert not kind or ref.kind == kind, f"{name}: the reference gives {ref.kind}, the probe was written for {kind}"
    build = work / "bin"
    r = subprocess.run(
        [str(cint), "build", "--root", str(work), "--toolchain", str(tc),
         "--cache", str(work / "cache"), "--out", str(build), name + ".ci"],
        capture_output=True, text=True, timeout=120,
    )
    if r.returncode != 0:
        raise AssertionError(f"build failed for {name}:\n{r.stderr}")
    fault = work / "fault.bin"
    fuel = work / "fuel.bin"
    exe = subprocess.run([str(build / name), str(fault), str(fuel)], capture_output=True, timeout=60)
    got_fuel = struct.unpack("<q", fuel.read_bytes()[-8:])[0]
    if ref.kind == "value":
        assert exe.returncode == 0, f"{name}: unexpected exit {exe.returncode}"
        assert exe.stdout == ref.stdout, f"{name}: stdout mismatch\ngot:  {exe.stdout!r}\nwant: {ref.stdout!r}"
        assert got_fuel == ref.fuel, f"{name}: fuel {got_fuel} != {ref.fuel}"
    else:
        assert ref.kind == "fault" and ref.record is not None
        assert exe.returncode == 1, f"{name}: expected fault exit 1, got {exe.returncode}"
        got_lines = decode_fault_record(fault.read_bytes(), RUN_TIME)[1].to_expect_lines(1)
        want_lines = ref.record.to_expect_lines(1)
        assert got_lines == want_lines, f"{name}: fault record mismatch\ngot:  {got_lines}\nwant: {want_lines}"
        assert got_fuel == ref.fuel, f"{name}: fault fuel {got_fuel} != {ref.fuel}"


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

    run_case(a.cint, tc, a.out / "valid", "valid", VALID_PROGRAM)

    emit_dir = a.out / "valid-ssa"
    subprocess.run(
        [str(a.cint), "emit-qbe", "--root", str(a.out / "valid"), "--toolchain", str(tc),
         "--out", str(emit_dir), "valid.ci"],
        check=True, capture_output=True, timeout=60,
    )
    stage = emit_dir.parent / (emit_dir / "MANIFEST.ref").read_text().split()[0]
    ssa = (stage / "valid.ssa").read_text(encoding="utf-8")
    ssa_lines = ssa.splitlines()
    assert "$cint_qbe_uabs_" not in ssa
    assert "$cint_qbe_minmax_" not in ssa
    cold_helpers = ("$cint_qbe_add_", "$cint_qbe_sub_", "$cint_qbe_mul_", "$cint_qbe_neg_",
                    "$cint_qbe_shift_", "$cint_qbe_as(")
    for idx, ln in enumerate(ssa_lines):
        if any(h in ln for h in cold_helpers):
            assert ln.endswith(", l %out)"), f"expected %out on cold helper: {ln}"
            assert ssa_lines[idx + 1].startswith("    jnz %ok, @u") and ssa_lines[idx + 1].endswith(", @fault"), (
                f"expected runtime helper branch after {ln}, got {ssa_lines[idx + 1]}"
            )
            assert "=l loadl %out" not in ssa_lines[idx + 2], f"unexpected hot load_out after cold block: {ln}"

    non_plain = a.out / "non-plain"
    non_plain.mkdir()
    (non_plain / "non_plain.ci").write_text(
        'import fs;\nrequires fs.read("/data");\n'
        'void main(Grants g) {\n'
        '    I64 s = fs.stat(g, "/data/a") catch -1;\n'
        '    I32 y = (s + 1) as I32;\n'
        '    "{y}\\n";\n'
        '}\n',
        encoding="utf-8",
    )
    np_emit = a.out / "non-plain-ssa"
    subprocess.run(
        [str(a.cint), "emit-qbe", "--root", str(non_plain), "--toolchain", str(tc),
         "--out", str(np_emit), "non_plain.ci"],
        check=True, capture_output=True, timeout=60,
    )
    np_stage = np_emit.parent / (np_emit / "MANIFEST.ref").read_text().split()[0]
    np_lines = (np_stage / "non_plain.ssa").read_text(encoding="utf-8").splitlines()
    add_idx = next(idx for idx, ln in enumerate(np_lines) if "$cint_qbe_add_i64(" in ln)
    assert np_lines[add_idx].endswith(", l %out)")
    assert np_lines[add_idx + 1].endswith(", @fault")
    assert "=l loadl %out" in np_lines[add_idx + 3]

    for name, source in FAULT_CASES:
        run_case(a.cint, tc, a.out / name, name, source)

    run_case(a.cint, tc, a.out / "limits", "limits", LIMIT_PROGRAM, "value")
    for name, source in LIMIT_FAULTS:
        run_case(a.cint, tc, a.out / name, name, source, "fault")

    run_case(a.cint, tc, a.out / "elide", "elide", ELIDE_PROGRAM, "value")
    el_emit = a.out / "elide-ssa"
    subprocess.run(
        [str(a.cint), "emit-qbe", "--root", str(a.out / "elide"), "--toolchain", str(tc),
         "--out", str(el_emit), "elide.ci"],
        check=True, capture_output=True, timeout=60,
    )
    el_stage = el_emit.parent / (el_emit / "MANIFEST.ref").read_text().split()[0]
    el_ssa = (el_stage / "elide.ssa").read_text(encoding="utf-8")
    assert "$cint_qbe_index" not in function_ssa(el_ssa, "fixedconst")
    for kept in ("fixedvar", "viewconst", "sizedconst"):
        assert "$cint_qbe_index(" in function_ssa(el_ssa, kept), f"{kept}: its index check is gone"

    print(f"PASS cpu-qbe scalar inline: 2 valid probes + {len(FAULT_CASES) + len(LIMIT_FAULTS)} fault-record probes "
          f"match cint_ref; 4 index-check forms; 1 non-plain probe")


if __name__ == "__main__":
    main()
