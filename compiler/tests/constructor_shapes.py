"""Compares constructor extent checks and argument order with cint_ref."""
import named_constructors as runner

SCALAR = 'struct R { I64[2] data; }\n'
RECORD = 'struct E { I64 value; }\nstruct R { E[2] data; }\n'
WILD = SCALAR + 'I64 read(in I64[_] a) { R r=R(a); return r.data[1]; }\n'
SIZED = SCALAR + 'I64 read[n](in I64[n] a) { R r=R(a); return r.data[1]; }\n'
VALUES = {}
for mode, body in (('wild', WILD), ('sized', SIZED)):
    for extent in (0, 1, 2, 3):
        VALUES[mode + '_' + str(extent)] = body + (
            'export I64 run() { I64[%d] a; return read(a); }' % extent)
for extent in (0, 2, 3):
    VALUES['record_' + str(extent)] = RECORD + (
        'I64 read(in E[_] a) { R r=R(a); return r.data[1].value; }\n'
        'export I64 run() { E[%d] a; return read(a); }' % extent)
VALUES.update({
    'empty_equal': 'struct R { I64[0] data; I64 value; }\nI64 read(in I64[_] a) { R r=R(a,7); return r.value; }\nexport I64 run() { I64[0] a; return read(a); }',
    'empty_mismatch': 'struct R { I64[0] data; I64 value; }\nI64 read(in I64[_] a) { R r=R(a,7); return r.value; }\nexport I64 run() { I64[1] a; return read(a); }',
    'bool_equal': 'struct R { Bool[2] data; }\nBool read(in Bool[_] a) { R r=R(a); return r.data[1]; }\nexport Bool run() { Bool[2] a; a[1]=true; return read(a); }',
    'bool_mismatch': 'struct R { Bool[2] data; }\nBool read(in Bool[_] a) { R r=R(a); return r.data[1]; }\nexport Bool run() { Bool[3] a; return read(a); }',
    'field_order': 'struct R { I64[2] first; I64[3] second; }\nI64 read(in I64[_] a, in I64[_] b) { R r=R(second=b,first=a); return 0; }\nexport I64 run() { I64[1] a; I64[4] b; return read(a,b); }',
    'second_field': 'struct R { I64[2] first; I64[3] second; }\nI64 read(in I64[_] a, in I64[_] b) { R r=R(second=b,first=a); return 0; }\nexport I64 run() { I64[2] a; I64[4] b; return read(a,b); }',
    'late_mutation': 'struct R { I64[2] data; I64 tag; }\nI64 mutate(inout I64[_] a) { a[1]=9; return 4; }\nI64 read(inout I64[_] a) { R r=R(a,mutate(a)); return r.data[1]*10+r.tag; }\nexport I64 run() { I64[2] a; a[1]=7; return read(a); }',
    'argument_fault_first': 'struct R { I64[2] data; I64 tag; }\nI64 fail(I64 n) { return I64.max+n; }\nI64 read(in I64[_] a) { R r=R(a,fail(1)); return 0; }\nexport I64 run() { I64[3] a; return read(a); }',
    'argument_bounds_first': 'struct R { I64[2] data; I64 tag; }\nI64 read(in I64[_] a) { I64 i=3; R r=R(a,a[i]); return 0; }\nexport I64 run() { I64[3] a; return read(a); }',
    'scalar_capture': 'struct R { I64 tag; I64[2] data; I64 last; }\nI64 mutate(inout I64[_] a) { a[0]=9; return 4; }\nI64 read(inout I64[_] a) { R r=R(a[0],a,mutate(a)); return r.tag*100+r.data[0]*10+r.last; }\nexport I64 run() { I64[2] a; a[0]=7; return read(a); }',
    'branch': SCALAR + 'I64 read(in I64[_] a, Bool yes) { if (yes) { R r=R(a); return r.data[1]; } return 4; }\nexport I64 run() { I64[2] a; a[1]=7; return read(a,true); }',
    'untaken_branch': SCALAR + 'I64 read(in I64[_] a, Bool yes) { if (yes) { R r=R(a); return r.data[1]; } return 4; }\nexport I64 run() { I64[3] a; return read(a,false); }',
    'loop': SCALAR + 'I64 read(in I64[_] a) { I64 total=0; for i in 0..3 { R r=R(a); total+=r.data[1]+i; } return total; }\nexport I64 run() { I64[2] a; a[1]=7; return read(a); }',
    'zero_record_equal': 'struct Empty { I64[0] none; }\nstruct R { Empty[2] data; I64 tag; }\nI64 read(in Empty[_] a) { R r=R(a,5); return r.tag; }\nexport I64 run() { Empty[2] a; return read(a); }',
    'zero_record_mismatch': 'struct Empty { I64[0] none; }\nstruct R { Empty[2] data; I64 tag; }\nI64 read(in Empty[_] a) { R r=R(a,5); return r.tag; }\nexport I64 run() { Empty[3] a; return read(a); }',
    'imported_equal':'I64 read(in records.E[_] a) { records.R r=records.R(data=a); return r.data[1].value; }\nexport I64 run() { records.E[2] a; a[1].value=7; return read(a); }',
    'imported_mismatch': 'I64 read(in records.E[_] a) { records.R r=records.R(data=a); return r.data[1].value; }\nexport I64 run() { records.E[3] a; return read(a); }',
})
DIAGNOSTICS = {
    'fixed_mismatch': SCALAR + 'I64 read(in I64[3] a) { R r=R(a); return 0; }\nexport I64 run() { I64[3] a; return read(a); }',
    'element_mismatch': SCALAR + 'I64 read(in U8[_] a) { R r=R(a); return 0; }\nexport I64 run() { U8[2] a; return read(a); }',
}

if __name__ == '__main__':
    raise SystemExit(runner.main(VALUES, DIAGNOSTICS, {},
        library='export struct E { I64 value; }\nexport struct R { E[2] data; }\n',
        label='constructor shapes'))
