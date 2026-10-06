"""Compares fixed-extent array constructor fields with the exact reference."""
import named_constructors as runner

SCALAR = 'struct R { I64[2] data; }\n'
RECORD = 'struct E { I64 value; }\nstruct R { E[2] data; }\n'
MUTATE = 'I64 mutate(inout I64[_] a) { a[1]=9; return 0; }\n'
VALUES = {
    'scalar': SCALAR + 'export I64 run() { I64[2] a; a[1]=7; R r=R(a); a[1]=9; return r.data[1]; }',
    'named': SCALAR + 'export I64 run() { I64[2] a; a[1]=7; R r=R(data=a); return r.data[1]; }',
    'record': RECORD + 'export I64 run() { E[2] a; a[1].value=7; R r=R(a); a[1].value=9; return r.data[1].value; }',
    'bool': 'struct R { Bool[2] data; }\nexport Bool run() { Bool[2] a; a[1]=true; R r=R(a); a[1]=false; return r.data[1]; }',
    'u8': 'struct R { U8[2] data; }\nexport I64 run() { U8[2] a; a[1]=7; R r=R(a); return r.data[1] as I64; }',
    'i32': 'struct R { I32[2] data; }\nexport I64 run() { I32[2] a; a[1]=7; R r=R(a); return r.data[1] as I64; }',
    'empty_scalar': 'struct R { I64[0] data; I64 value; }\nexport I64 run() { I64[0] a; R r=R(a,7); return r.value; }',
    'empty_record': 'struct E { I64 value; }\nstruct R { E[0] data; I64 value; }\nexport I64 run() { E[0] a; R r=R(a,7); return r.value; }',
    'field': SCALAR + 'export I64 run() { R a; a.data[1]=7; R r=R(a.data); a.data[1]=9; return r.data[1]; }',
    'nested_field': SCALAR + 'struct O { R inner; }\nexport I64 run() { O a; a.inner.data[1]=7; R r=R(a.inner.data); return r.data[1]; }',
    'indexed_field': SCALAR + 'export I64 run() { R[2] a; a[1].data[1]=7; R r=R(a[1].data); return r.data[1]; }',
    'fixed_view': SCALAR + 'I64 read(in I64[2] a) { R r=R(a); return r.data[1]; }\nexport I64 run() { I64[2] a; a[1]=7; return read(a); }',
    'fixed_record_view': RECORD + 'I64 read(in E[2] a) { R r=R(a); return r.data[1].value; }\nexport I64 run() { E[2] a; a[1].value=7; return read(a); }',
    'late_capture': 'struct R { I64[2] data; I64 tag; }\n' + MUTATE + 'export I64 run() { I64[2] a; a[1]=7; R r=R(a,mutate(a)); return r.data[1]; }',
    'named_late_capture': 'struct R { I64 tag; I64[2] data; }\n' + MUTATE + 'export I64 run() { I64[2] a; a[1]=7; R r=R(data=a,tag=mutate(a)); return r.data[1]; }',
    'index_capture': 'struct R { I64[2] data; I64 tag; }\nI64 mutate(inout I64[1] i) { i[0]=0; return 0; }\nexport I64 run() { R[2] a; a[0].data[1]=9; a[1].data[1]=7; I64[1] i; i[0]=1; R r=R(a[i[0]].data,mutate(i)); return r.data[1]; }',
    'base_once': SCALAR + 'I64 step(inout I64[1] i) { i[0]+=1; return i[0]-1; }\nexport I64 run() { R[2] a; a[0].data[1]=7; a[1].data[1]=9; I64[1] i; R r=R(a[step(i)].data); return r.data[1]*10+i[0]; }',
    'nested_snapshot': SCALAR + 'struct O { R inner; I64 tag; }\n' + MUTATE + 'export I64 run() { I64[2] a; a[1]=7; O r=O(R(a),mutate(a)); return r.inner.data[1]; }',
    'multiple_fields': 'struct R { I64[2] a; I64 tag; I64[2] b; }\n' + MUTATE + 'export I64 run() { I64[2] a; a[1]=7; I64[2] b; b[1]=3; R r=R(b=b,a=a,tag=mutate(a)); return r.a[1]*10+r.b[1]; }',
    'mixed_record_array': RECORD + 'struct O { E leaf; E[2] data; I64 tag; }\nI64 mutate(inout E[2] a) { a[0].value=4; a[1].value=9; return 0; }\nexport I64 run() { E[2] a; a[0].value=2; a[1].value=7; O r=O(a[0],a,mutate(a)); return r.leaf.value*10+r.data[1].value; }',
    'imported': 'export I64 run() { records.E[2] a; a[1].value=7; records.R r=records.R(data=a); return r.data[1].value; }',
    'early_bounds': SCALAR + 'export I64 run() { R[1] a; I64 i=1; R r=R(a[i].data); return r.data[1]; }',
    'empty_base_bounds': 'struct R { I64[0] data; I64 value; }\nexport I64 run() { R[1] a; I64 i=1; R r=R(a[i].data,7); return r.value; }',
    'empty_base_effect': 'struct R { I64[0] data; I64 value; }\nI64 step(inout I64[1] i) { i[0]+=1; return 0; }\nexport I64 run() { R[1] a; I64[1] i; R r=R(a[step(i)].data,7); return r.value+i[0]; }',
    'later_fault': 'struct R { I64[2] data; I64 tag; }\nI64 fail(I64 a) { return I64.max+a; }\nexport I64 run() { I64[2] a; R r=R(a,fail(1)); return r.data[1]; }',
    'early_bounds_before_fault': 'struct R { I64[2] data; I64 tag; }\nI64 fail(I64 a) { return I64.max+a; }\nexport I64 run() { R[1] a; I64 i=1; R r=R(a[i].data,fail(1)); return r.data[1]; }',
}
DIAGNOSTICS = {
    'wrong_extent': SCALAR + 'export I64 run() { I64[3] a; R r=R(a); return 0; }',
    'wrong_element': SCALAR + 'export I64 run() { U8[2] a; R r=R(a); return 0; }',
    'wrong_nominal': RECORD + 'struct Other { I64 value; }\nexport I64 run() { Other[2] a; R r=R(a); return 0; }',
    'missing_field': SCALAR + 'export I64 run() { R r=R(); return 0; }',
    'duplicate_field': SCALAR + 'export I64 run() { I64[2] a; R r=R(data=a,data=a); return 0; }',
    'wrong_literal': SCALAR + 'export I64 run() { R r=R(7); return 0; }',
}

if __name__ == '__main__':
    raise SystemExit(runner.main(VALUES, DIAGNOSTICS, {},
        library='export struct E { I64 value; }\nexport struct R { E[2] data; }\n',
        label='array constructors'))
