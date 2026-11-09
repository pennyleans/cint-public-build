"""Writes the generated cases of cint-as (docs/design/notes/2026-11-07-cint-as.md section 6).

python toolchain/as/tests/gen_cases.py [--check]

Each case is GNU assembler text in the dialect of section 3: every mnemonic, size, register and operand form
(forms-*.s), data and sections (data.s), and jump chains across alignments (relax-*.s, from fixed seeds). The
files are committed; --check fails when a committed file differs from what this script writes.
"""
import argparse
from pathlib import Path
import random
import sys

CASES = Path(__file__).resolve().parent / "cases"

R64 = "rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()
R32 = "eax ecx edx ebx esp ebp esi edi r8d r9d r10d r11d r12d r13d r14d r15d".split()
R16 = "ax cx dx bx sp bp si di r8w r9w r10w r11w r12w r13w r14w r15w".split()
R8 = "al cl dl bl spl bpl sil dil r8b r9b r10b r11b r12b r13b r14b r15b".split()
REGS = {"b": R8, "w": R16, "l": R32, "q": R64}
XMM = [f"xmm{i}" for i in range(16)]
CCS = "o no b c nae ae nb nc e z ne nz be na a nbe s ns p pe np po l nge ge nl le ng g nle".split()
MEMS = ["(%rax)", "(%rsp)", "(%rbp)", "(%r12)", "(%r13)", "(%r15)", "8(%rbx)", "-8(%rbp)", "-128(%rcx)",
        "-129(%rdx)", "127(%rsi)", "128(%rdi)", "4096(%r8)", "(%rax, %rcx)", "(%rax,%rcx,2)", "8(%rsp, %rbp, 4)",
        "-16(%r13, %r14, 8)", "(, %rcx, 8)", "16(, %r9, 4)", "1234", "glob(%rip)", "loc+8(%rip)", "ext-4(%rip)",
        "glob(%rax)", "ext+16(%rbx, %rcx, 2)", "%fs:0", "%fs:8(%rax)"]
IMMS = ["$0", "$1", "$-1", "$127", "$128", "$-128", "$-129", "$255", "$4096", "$65535", "$-32768", "$2147483647",
        "$-2147483648"]
ALU = "add or adc sbb and sub xor cmp".split()

HEAD = ".text\n"
TAIL = """.data
.balign 8
.globl glob
glob:
\t.quad 0
loc:
\t.quad 0
.section .tbss,"awT",@nobits
.balign 8
tv:
\t.fill 8,1,0
.section .note.GNU-stack,"",@progbits
"""


def imm_fits(imm, suffix):
    v = int(imm[1:])
    if suffix == "q":
        return -2**31 <= v < 2**31
    bits = {"b": 8, "w": 16, "l": 32}[suffix]
    return -2**(bits - 1) <= v < 2**bits


def forms_alu():
    out = []
    for op in ALU + ["test"]:
        for sfx, regs in REGS.items():
            for imm in IMMS:
                if imm_fits(imm, sfx):
                    for r in regs:
                        out.append(f"\t{op}{sfx} {imm}, %{r}")
                    for m in MEMS[:12] + MEMS[19:21]:
                        out.append(f"\t{op}{sfx} {imm}, {m}")
            for a in regs:
                for b in regs:
                    out.append(f"\t{op}{sfx} %{a}, %{b}")
            for m in MEMS:
                out.append(f"\t{op}{sfx} %{regs[3]}, {m}")
                out.append(f"\t{op}{sfx} %{regs[12]}, {m}")
                out.append(f"\t{op}{sfx} {m}, %{regs[0]}")
                out.append(f"\t{op}{sfx} {m}, %{regs[9]}")
            out.append(f"\t{op} %{regs[1]}, %{regs[2]}")
    out.append("\taddq $glob, %rax")
    out.append("\taddl $glob, %ecx")
    out.append("\tcmpq $ext+8, %rax")
    return "\n".join(out) + "\n"


def forms_int():
    out = []
    for sfx in "wlq":
        regs = REGS[sfx]
        for a in regs:
            out.append(f"\timul{sfx} %{a}, %{regs[5]}")
            out.append(f"\timul{sfx} %{regs[11]}, %{a}")
        for m in MEMS:
            out.append(f"\timul{sfx} {m}, %{regs[0]}")
            out.append(f"\timul{sfx} $3, {m}, %{regs[13]}")
            out.append(f"\timul{sfx} $300, {m}, %{regs[2]}")
        for imm in ["$1", "$-1", "$127", "$128", "$-129", "$4096"]:
            for r in regs:
                out.append(f"\timul{sfx} {imm}, %{r}, %{regs[7]}")
                out.append(f"\timul{sfx} {imm}, %{r}")
    for op in "shl sal shr sar".split():
        for sfx, regs in REGS.items():
            for r in regs:
                out.append(f"\t{op}{sfx} $1, %{r}")
                out.append(f"\t{op}{sfx} $5, %{r}")
                out.append(f"\t{op}{sfx} %cl, %{r}")
            for m in MEMS[:10]:
                out.append(f"\t{op}{sfx} $1, {m}")
                out.append(f"\t{op}{sfx} $63, {m}")
                out.append(f"\t{op}{sfx} %cl, {m}")
            out.append(f"\t{op} %cl, %{regs[3]}")
            out.append(f"\t{op} $2, %{regs[9]}")
    for op in "neg not div idiv mul".split():
        for sfx, regs in REGS.items():
            for r in regs:
                out.append(f"\t{op}{sfx} %{r}")
            for m in MEMS:
                out.append(f"\t{op}{sfx} {m}")
    for cc in CCS:
        out.append(f"\tset{cc} %al")
        out.append(f"\tset{cc} %dil")
        out.append(f"\tset{cc} %r11b")
        out.append(f"\tset{cc} -1(%rbp)")
        for sfx in "wlq":
            regs = REGS[sfx]
            out.append(f"\tcmov{cc} %{regs[1]}, %{regs[0]}")
            out.append(f"\tcmov{cc} %{regs[14]}, %{regs[6]}")
            out.append(f"\tcmov{cc} -8(%rbp), %{regs[10]}")
    for sfx in "wlq":
        regs = REGS[sfx]
        for a in regs:
            for b in (regs[0], regs[5], regs[12]):
                out.append(f"\txchg{sfx} %{a}, %{b}")
                out.append(f"\txchg{sfx} %{b}, %{a}")
        out.append(f"\txchg{sfx} %{regs[3]}, (%rax)")
        out.append(f"\txchg{sfx} 8(%rsp), %{regs[9]}")
    out.append("\txchgb %al, %cl")
    out.append("\txchgb %sil, %r9b")
    for r in R64:
        out.append(f"\tpushq %{r}")
        out.append(f"\tpopq %{r}")
        out.append(f"\tpush %{r}")
        out.append(f"\tcallq *%{r}")
        out.append(f"\tjmp *%{r}")
    out.append("\tcallq *8(%rax)")
    out.append("\tcallq *glob(%rip)")
    for f in "ret leave ud2 endbr64 cltd cqto cltq nop".split():
        out.append(f"\t{f}")
    out.append("\tcallq ext")
    out.append("\tcallq ext@plt")
    out.append("\tcall glob")
    out.append("\tcallq here")
    out.append("here:")
    out.append("\tcallq here")
    out.append("\tcallq other")
    out.append("\tcallq gfun")
    out.append(".globl gfun")
    out.append("gfun:")
    out.append("\tret")
    out.append(".section .text.other,\"ax\",@progbits")
    out.append("other:")
    out.append("\tcallq here")
    out.append("\tret")
    out.append(".text")
    return "\n".join(out) + "\n"


def forms_mov():
    out = []
    for sfx, regs in REGS.items():
        for a in regs:
            for b in regs:
                out.append(f"\tmov{sfx} %{a}, %{b}")
        for r in regs:
            for imm in IMMS:
                if imm_fits(imm, sfx):
                    out.append(f"\tmov{sfx} {imm}, %{r}")
        for m in MEMS:
            out.append(f"\tmov{sfx} %{regs[0]}, {m}")
            out.append(f"\tmov{sfx} %{regs[6]}, {m}")
            out.append(f"\tmov{sfx} %{regs[13]}, {m}")
            out.append(f"\tmov{sfx} {m}, %{regs[2]}")
            out.append(f"\tmov{sfx} {m}, %{regs[8]}")
            out.append(f"\tmov{sfx} $7, {m}")
            out.append(f"\tmov{sfx} $-100, {m}")
        out.append(f"\tmov %{regs[4]}, %{regs[11]}")
    for r in R64:
        for imm in ["$4294967295", "$4294967296", "$-2147483649", "$81985529216486895", "$-9223372036854775808",
                    "$9223372036854775807", "$0"]:
            out.append(f"\tmovq {imm}, %{r}")
            out.append(f"\tmovabsq {imm}, %{r}")
    for r in R32:
        out.append(f"\tmovl $4294967295, %{r}")
        out.append(f"\tmovl $-4294967296, %{r}")
    out.append("\tmovl $-4294967296, -8(%rbp)")
    out.append("\tmovl $81985529216486895>>32, 4+-8(%rbp)")
    out.append("\tmovl $-81985529216486895>>32, 4+-16(%rbp)")
    out.append("\tmovq $glob, %rax")
    out.append("\tmovl $glob, %r9d")
    out.append("\tmovq $loc+8, -8(%rbp)")
    out.append("\tmovq ext@gotpcrel(%rip), %rax")
    out.append("\tmovq ext@gotpcrel(%rip), %r12")
    out.append("\tmovl ext@gotpcrel(%rip), %ecx")
    out.append("\tmovq %fs:0, %rax")
    out.append("\tmovq %fs:0, %r13")
    out.append("\tmovq %fs:tv@tpoff, %rax")
    out.append("\tmovl %fs:tv@tpoff, %ecx")
    out.append("\tleaq tv@tpoff(%rax), %rax")
    out.append("\tleaq tv@tpoff+8(%r11), %r11")
    out.append("\taddq tie@gottpoff(%rip), %rax")
    out.append("\taddq tie@gottpoff(%rip), %r10")
    xs = ["movsbw", "movsbl", "movsbq", "movswl", "movswq", "movslq", "movzbw", "movzbl", "movzbq", "movzwl", "movzwq"]
    size = {"b": R8, "w": R16, "l": R32, "q": R64}
    for x in xs:
        src = size[x[4]] if x != "movslq" else R32
        dst = size[x[-1]]
        for i in range(16):
            out.append(f"\t{x} %{src[i]}, %{dst[(i * 5) % 16]}")
        for m in MEMS[:12]:
            out.append(f"\t{x} {m}, %{dst[3]}")
            out.append(f"\t{x} {m}, %{dst[10]}")
    for sfx in "wlq":
        regs = REGS[sfx]
        for m in MEMS:
            if not m.startswith("%fs"):
                out.append(f"\tlea{sfx} {m}, %{regs[1]}")
                out.append(f"\tlea{sfx} {m}, %{regs[14]}")
    return "\n".join(out) + "\n"


def forms_sse():
    out = []
    for op in ["movss", "movsd", "movaps"]:
        for i in range(16):
            out.append(f"\t{op} %{XMM[i]}, %{XMM[(i * 7) % 16]}")
        for m in MEMS:
            out.append(f"\t{op} {m}, %xmm3")
            out.append(f"\t{op} {m}, %xmm12")
            out.append(f"\t{op} %xmm5, {m}")
            out.append(f"\t{op} %xmm15, {m}")
    for i in range(16):
        out.append(f"\tmovq %{R64[i]}, %{XMM[(i + 3) % 16]}")
        out.append(f"\tmovq %{XMM[i]}, %{R64[(i + 5) % 16]}")
        out.append(f"\tmovq %{XMM[i]}, %{XMM[(i + 9) % 16]}")
    for m in MEMS[:12] + MEMS[20:22]:
        out.append(f"\tmovq {m}, %xmm0")
        out.append(f"\tmovq {m}, %xmm9")
        out.append(f"\tmovq %xmm1, {m}")
        out.append(f"\tmovq %xmm14, {m}")
    for op in "addss addsd subss subsd mulss mulsd divss divsd cvtss2sd cvtsd2ss ucomiss ucomisd xorps xorpd".split():
        for i in range(16):
            out.append(f"\t{op} %{XMM[i]}, %{XMM[(i * 3 + 1) % 16]}")
        for m in MEMS[:12] + MEMS[20:22]:
            out.append(f"\t{op} {m}, %xmm2")
            out.append(f"\t{op} {m}, %xmm11")
    for op in ["cvttss2si", "cvttsd2si"]:
        for i in range(16):
            out.append(f"\t{op} %{XMM[i]}, %{R32[(i + 2) % 16]}")
            out.append(f"\t{op} %{XMM[i]}, %{R64[(i + 4) % 16]}")
            out.append(f"\t{op}l %{XMM[i]}, %{R32[(i + 6) % 16]}")
            out.append(f"\t{op}q %{XMM[i]}, %{R64[(i + 8) % 16]}")
        out.append(f"\t{op} -8(%rbp), %eax")
        out.append(f"\t{op}q 8(%r12), %r9")
    for op in ["cvtsi2ss", "cvtsi2sd"]:
        for i in range(16):
            out.append(f"\t{op} %{R32[i]}, %{XMM[(i + 1) % 16]}")
            out.append(f"\t{op} %{R64[i]}, %{XMM[(i + 2) % 16]}")
            out.append(f"\t{op}l %{R32[i]}, %{XMM[(i + 3) % 16]}")
            out.append(f"\t{op}q %{R64[i]}, %{XMM[(i + 4) % 16]}")
        out.append(f"\t{op} -8(%rbp), %xmm0")
        out.append(f"\t{op}l -8(%rbp), %xmm8")
        out.append(f"\t{op}q 16(%rax,%rcx,4), %xmm13")
    out.append("\txorps .Lfp0(%rip), %xmm0")
    out.append("\txorpd .Lfp1(%rip), %xmm7")
    out.append("/* floating point constants */")
    out.append(".section .rodata")
    out.append(".p2align 4")
    out.append(".Lfp1:")
    out.append("\t.quad -9223372036854775808")
    out.append("\t.quad 0")
    out.append("")
    out.append(".section .rodata")
    out.append(".p2align 2")
    out.append(".Lfp0:")
    out.append("\t.int -2147483648 /* -0.000000 */")
    out.append(".text")
    return "\n".join(out) + "\n"


def data():
    return """.data
.balign 8
.globl d0
d0:
\t.byte 1
\t.byte 255, -1, 128
\t.short 7, -2, 65535
\t.int 1, -1, 4294967295
\t.quad 1, -1, 9223372036854775807, -9223372036854775808
\t.quad d1
\t.quad d1+8
\t.quad d0-8
\t.quad ext
\t.quad ext+24
\t.int d1
\t.int ext
\t.short d1
\t.byte d1
\t.ascii "hi\\n\\000\\"x\\\\ # ; /* tab\\t\\x41\\101\\7"
\t.ascii "a", "b"
\t.fill 3,1,0
\t.fill 2,4,0x01020304
\t.fill 2,8,7
\t.zero 5
\t.p2align 4
d1:
\t.quad 0x10, 010, 0b101
.bss
.balign 32
b0:
\t.fill 100,1,0
.balign 8
b1:
\t.zero 16
.section .rodata
r0:
\t.int 3
.section .data.rel.ro,"aw"
.balign 8
\t.quad r0
\t.quad b1+4
.section .init_array,"aw"
\t.quad f0
.section .tdata,"awT",@progbits
.balign 8
t0:
\t.quad 9
.section .tbss,"awT",@nobits
t1:
\t.fill 4,1,0
.comm c0,3
.comm c1,24
.comm c2,100,32
.comm c3,8,4
.text
f0:
\tret
.type f0, @function
.size f0, .-f0
.type d0, @object
.size d0, 8
.section .note.GNU-stack,"",@progbits
"""


FILL = ["\tmovl $1, %eax", "\tmovq %rax, %rbx", "\tret", "\tmovabsq $81985529216486895, %rcx",
        "\tleaq glob(%rip), %rax", "\taddq $1000, %rdi", "\tcallq ext", "\tnop", "\tpushq %r12",
        "\tmovl %eax, 1000(%rsp, %r12, 8)"]


def relax(seed):
    rnd = random.Random(seed)
    nlab = 40
    out = [".text", ".globl f", "f:"]
    for i in range(nlab):
        if rnd.random() < 0.3:
            out.append(".p2align 4")
        out.append(f".Lb{i}:")
        for _ in range(rnd.randrange(0, 30)):
            out.append(rnd.choice(FILL))
            if rnd.random() < 0.15:
                t = rnd.randrange(nlab)
                j = rnd.choice(["jmp"] + ["j" + c for c in CCS])
                out.append(f"\t{j} .Lb{t}")
    out.append("\tret")
    out.append(".type f, @function")
    out.append(".size f, .-f")
    return "\n".join(out) + "\n"


def edges():
    out = [".text", "e:"]
    # A jump at each distance around the short range, both ways, with and without an alignment between.
    for d in range(120, 136):
        out.append(f"\tjmp .Lf{d}")
        out.append(f"\t.fill {d},1,0x90")
        out.append(f".Lf{d}:")
        out.append(f".Lr{d}:")
        out.append(f"\t.fill {d},1,0x90")
        out.append(f"\tjne .Lr{d}")
    for d in range(100, 140, 3):
        out.append(f"\tje .La{d}")
        out.append(f"\t.fill {d - 20},1,0x90")
        out.append(".p2align 4")
        out.append(f"\t.fill 7,1,0x90")
        out.append(f".La{d}:")
    out.append("\tret")
    return "\n".join(out) + "\n"


def padding():
    """Alignment fill (G-10): NOP patterns, the plain NOP after data, and the jump over long padding."""
    out = [".text"]
    pads = list(range(1, 16)) + list(range(84, 100)) + list(range(125, 135)) + [200, 255]
    for raw in (0, 1):
        for p in pads:
            body = 256 - p
            out.append("\t.balign 256")
            n = body - raw
            out += ["\tmovabsq $0, %rax"] * (n // 10) + ["\tnop"] * (n % 10)
            if raw:
                out.append("\t.byte 0x90")
            out.append("\t.balign 256")
    for p in (300, 4000):
        out.append("\t.balign 4096")
        out += ["\tmovabsq $0, %rax"] * ((4096 - p) // 10) + ["\tnop"] * ((4096 - p) % 10)
        out.append("\t.balign 4096")
    out.append("\tret")
    return "\n".join(out) + "\n"


def cases():
    found = {
        "forms-alu.s": HEAD + forms_alu() + TAIL,
        "forms-int.s": HEAD + forms_int() + TAIL,
        "forms-mov.s": HEAD + forms_mov() + TAIL,
        "forms-sse.s": HEAD + forms_sse() + TAIL,
        "data.s": data(),
        "relax-edges.s": edges(),
        "padding.s": padding(),
    }
    for seed in range(1, 9):
        found[f"relax-{seed}.s"] = relax(seed)
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    bad = 0
    for name, body in cases().items():
        path = CASES / name
        if a.check:
            if not path.exists() or path.read_text() != body:
                print(f"differs: {path}")
                bad += 1
        else:
            path.write_text(body)
    if a.check:
        print(f"{'FAIL' if bad else 'PASS'} gen_cases: {len(cases())} generated cases, {bad} differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
