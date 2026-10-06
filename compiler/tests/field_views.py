"""Compares record-array field borrowing with the exact reference."""
import named_constructors as runner

SCALAR = 'struct R { I64[2] data; }\n'
RECORD = 'struct E { I64 value; }\nstruct R { E[2] data; }\n'
TAKE = 'I64 take[n](in I64[n] a) { return a[1]+n; }\n'
VALUES = {
    'field': SCALAR + TAKE + 'export I64 run() { R r; r.data[1]=7; return take(r.data); }',
    'nested': SCALAR + 'struct O { R inner; }\n' + TAKE + 'export I64 run() { O r; r.inner.data[1]=7; return take(r.inner.data); }',
    'indexed': SCALAR + TAKE + 'export I64 run() { R[2] r; r[1].data[1]=7; return take(r[1].data); }',
    'nested_indexed': SCALAR + 'struct O { R[2] inner; }\n' + TAKE + 'export I64 run() { O r; r.inner[1].data[1]=7; return take(r.inner[1].data); }',
    'mutable': SCALAR + 'void put(inout I64[_] a) { a[1]=7; }\nexport I64 run() { R r; put(r.data); return r.data[1]; }',
    'record': RECORD + 'I64 take[n](in E[n] a) { return a[1].value+n; }\nexport I64 run() { R r; r.data[1].value=7; return take(r.data); }',
    'record_mutable': RECORD + 'void put(inout E[_] a) { a[1].value=7; }\nexport I64 run() { R r; put(r.data); return r.data[1].value; }',
    'bool': 'struct R { Bool[2] data; }\nBool take(in Bool[2] a) { return a[1]; }\nexport Bool run() { R r; r.data[1]=true; return take(r.data); }',
    'empty': 'struct R { I64[0] data; I64 value; }\nI64 take[n](in I64[n] a) { return n; }\nexport I64 run() { R r; return take(r.data); }',
    'empty_record': 'struct E { I64 value; }\nstruct R { E[0] data; I64 value; }\nI64 take[n](in E[n] a) { return n; }\nexport I64 run() { R r; return take(r.data); }',
    'empty_bounds': 'struct R { I64[0] data; I64 value; }\nI64 take[n](in I64[n] a) { return a[0]; }\nexport I64 run() { R r; return take(r.data); }',
    'empty_record_bounds': 'struct E { I64 value; }\nstruct R { E[0] data; I64 value; }\nI64 take[n](in E[n] a) { return a[0].value; }\nexport I64 run() { R r; return take(r.data); }',
    'borrow_capture': SCALAR + 'I64 mutate(inout I64[2] a) { a[1]=9; return 0; }\nI64 take(in I64[2] a,I64 ignored) { return a[1]; }\nexport I64 run() { R r; r.data[1]=7; return take(r.data,mutate(r.data)); }',
    'index_capture': SCALAR + 'I64 mutate(inout I64[1] i) { i[0]=0; return 0; }\nI64 take(in I64[2] a,I64 ignored) { return a[1]; }\nexport I64 run() { R[2] r; I64[1] i; i[0]=1; r[0].data[1]=9; r[1].data[1]=7; return take(r[i[0]].data,mutate(i)); }',
    'base_once': SCALAR + 'I64 step(inout I64[1] c) { c[0]+=1; return c[0]-1; }\nI64 take(in I64[2] a,I64 seen) { return a[1]*10+seen; }\nexport I64 run() { R[2] rows; rows[0].data[1]=7; rows[1].data[1]=9; I64[1] c; return take(rows[step(c)].data,c[0]); }',
    'distinct_fields': 'struct R { I64[2] a; I64[2] b; }\nvoid put(inout I64[_] a,in I64[_] b) { a[1]=b[1]; }\nexport I64 run() { R r; r.b[1]=7; put(r.a,r.b); return r.a[1]; }',
    'readonly_same': SCALAR + 'I64 take(in I64[_] a,in I64[_] b) { return a[1]+b[1]; }\nexport I64 run() { R r; r.data[1]=7; return take(r.data,r.data); }',
    'indexed_bounds_before_call': SCALAR + 'I64 take(in I64[_] a) { return a[0]; }\nexport I64 run() { R[2] r; I64 i=2; return take(r[i].data); }',
    'indexed_negative_before_call': SCALAR + 'I64 take(in I64[_] a) { return a[0]; }\nexport I64 run() { R[2] r; I64 i=-1; return take(r[i].data); }',
    'empty_distinct_fields': 'struct R { I64[0] a; I64[0] b; I64 value; }\nI64 take[n](inout I64[n] a,in I64[n] b) { return n; }\nexport I64 run() { R r; return take(r.a,r.b); }',
    'indexed_bounds_before_later_argument': SCALAR + 'I64 fail(I64 x) { return I64.max+x; }\nI64 take(in I64[_] a,I64 ignored) { return a[0]; }\nexport I64 run() { R[2] r; I64 i=2; return take(r[i].data,fail(1)); }',
    'forwarded_view': SCALAR + TAKE + 'I64 forward(in R[_] r) { return take(r[1].data); }\nexport I64 run() { R[2] r; r[1].data[1]=7; return forward(r); }',
    'fixed_shape': SCALAR + 'I64 take(in I64[2] a) { return a[1]; }\nexport I64 run() { R r; r.data[1]=7; return take(r.data); }',
    'shape_pair': SCALAR + 'I64 take[n](in I64[n] a,in I64[n] b) { return n+a[1]+b[1]; }\nexport I64 run() { R r; I64[2] b; r.data[1]=7; return take(r.data,b); }',
    'shape_dynamic_equal': SCALAR + 'I64 take[n](in I64[n] a,in I64[n] b) { return n; }\nI64 forward(in I64[_] a) { R r; return take(a,r.data); }\nexport I64 run() { I64[2] a; return forward(a); }',
    'shape_dynamic_different': SCALAR + 'I64 take[n](in I64[n] a,in I64[n] b) { return n; }\nI64 forward(in I64[_] a) { R r; return take(a,r.data); }\nexport I64 run() { I64[3] a; return forward(a); }',
    'imported_type': 'export I64 run() { records.R r; r.data[1]=7; return records.take(r.data); }',
}
DIAGNOSTICS = {
    'readonly_write': SCALAR + 'void put(inout I64[_] a) { a[1]=7; }\nI64 read(R r) { put(r.data); return 0; }\nexport I64 run() { R r; return read(r); }',
    'readonly_record_view': SCALAR + 'void put(inout I64[_] a) { a[1]=7; }\nI64 read(in R[_] r) { put(r[0].data); return 0; }\nexport I64 run() { R[1] r; return read(r); }',
    'static_same': SCALAR + 'void put(inout I64[_] a,in I64[_] b) { a[1]=b[1]; }\nexport I64 run() { R r; put(r.data,r.data); return 0; }',
    'empty_static_same': 'struct R { I64[0] data; I64 value; }\nI64 take[n](inout I64[n] a,in I64[n] b) { return n; }\nexport I64 run() { R r; return take(r.data,r.data); }',
    'shape_static': SCALAR + 'I64 take(in I64[3] a) { return 0; }\nexport I64 run() { R r; return take(r.data); }',
    'wrong_element': SCALAR + 'I64 take(in U8[_] a) { return 0; }\nexport I64 run() { R r; return take(r.data); }',
    'missing_field': SCALAR + TAKE + 'export I64 run() { R r; return take(r.absent); }',
    'wrong_nominal': RECORD + 'struct Other { I64 value; }\nI64 take(in Other[_] a) { return 0; }\nexport I64 run() { R r; return take(r.data); }',
}

if __name__ == '__main__':
    raise SystemExit(runner.main(VALUES, DIAGNOSTICS, {},
        library='export struct R { I64[2] data; }\nexport I64 take(in I64[_] a) { return a[1]; }\n',
        label='field views'))
