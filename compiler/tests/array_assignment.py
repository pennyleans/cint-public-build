"""Compares whole-array assignment and its copy.shape checks with cint_ref."""
import named_constructors as runner

RECORD = 'struct E { I64 value; }\n'
FIELD = 'struct R { I64[2] data; I64 tag; }\n'
ZERO = 'struct Z { I64[0] none; }\n'
STEP = 'I64 step(inout I64[1] k) { k[0] += 1; return k[0] - 1; }\n'
MOVE = 'void move(inout I64[_] dst, in I64[_] src) { dst = src; }\n'
VALUES = {
    'local': 'export I64 run() { I64[2] a; a[1] = 7; I64[2] b; b = a; a[1] = 9; return b[1]; }',
    'self': 'export I64 run() { I64[2] a; a[1] = 7; a = a; return a[1]; }',
    'view_source': 'I64 read(in I64[_] src) { I64[2] d; d = src; return d[1]; }\n'
                   'export I64 run() { I64[2] a; a[1] = 7; return read(a); }',
    'view_source_mismatch': 'I64 read(in I64[_] src) { I64[2] d; d = src; return d[1]; }\n'
                            'export I64 run() { I64[3] a; return read(a); }',
    'view_target': 'void put(inout I64[_] dst) { I64[2] s; s[1] = 7; dst = s; }\n'
                   'export I64 run() { I64[2] a; put(a); return a[1]; }',
    'view_target_mismatch': 'void put(inout I64[_] dst) { I64[2] s; s[1] = 7; dst = s; }\n'
                            'export I64 run() { I64[4] a; put(a); return a[1]; }',
    'views_equal': MOVE + 'export I64 run() { I64[3] d; I64[3] s; s[2] = 7; move(d, s); return d[2]; }',
    'views_mismatch': MOVE + 'export I64 run() { I64[2] d; I64[3] s; move(d, s); return 0; }',
    'views_empty': MOVE + 'export I64 run() { I64[0] d; I64[0] s; move(d, s); return 4; }',
    'views_empty_source': MOVE + 'export I64 run() { I64[1] d; I64[0] s; move(d, s); return 0; }',
    'views_empty_target': MOVE + 'export I64 run() { I64[0] d; I64[1] s; move(d, s); return 0; }',
    'sized': 'void move[n](inout I64[n] dst, in I64[n] src) { dst = src; }\n'
             'export I64 run() { I64[2] d; I64[2] s; s[1] = 7; move(d, s); return d[1]; }',
    'fixed_views': 'void move(inout I64[2] dst, in I64[2] src) { dst = src; }\n'
                   'export I64 run() { I64[2] d; I64[2] s; s[1] = 7; move(d, s); return d[1]; }',
    'local_empty': 'export I64 run() { I64[0] a; I64[0] b; a = b; return 3; }',
    'bool': 'void move(inout Bool[_] dst, in Bool[_] src) { dst = src; }\n'
            'export Bool run() { Bool[2] d; Bool[2] s; s[1] = true; move(d, s); return d[1]; }',
    'u8_mismatch': 'void move(inout U8[_] dst, in U8[_] src) { dst = src; }\n'
                   'export I64 run() { U8[1] d; U8[2] s; move(d, s); return 0; }',
    'records': RECORD + 'export I64 run() { E[2] a; a[1].value = 7; E[2] b; b = a; a[1].value = 9; return b[1].value; }',
    'record_views': RECORD + 'void move(inout E[_] dst, in E[_] src) { dst = src; }\n'
                    'export I64 run() { E[2] d; E[2] s; s[1].value = 7; move(d, s); return d[1].value; }',
    'record_views_mismatch': RECORD + 'void move(inout E[_] dst, in E[_] src) { dst = src; }\n'
                             'export I64 run() { E[3] d; E[2] s; move(d, s); return 0; }',
    'imported_records': 'void move(inout records.E[_] dst, in records.E[_] src) { dst = src; }\n'
                        'export I64 run() { records.E[2] d; records.E[2] s; s[1].value = 7; move(d, s); '
                        'return d[1].value; }',
    'field_target': FIELD + 'export I64 run() { R r; I64[2] a; a[1] = 7; r.data = a; a[1] = 9; return r.data[1]; }',
    'field_source': FIELD + 'export I64 run() { R r; r.data[1] = 7; I64[2] b; b = r.data; return b[1]; }',
    'field_to_field': FIELD + 'export I64 run() { R r; R q; r.data[1] = 7; q.data = r.data; return q.data[1]; }',
    'field_view': FIELD + 'void set(inout R r, in I64[_] a) { r.data = a; }\n'
                  'export I64 run() { R r; I64[2] a; a[1] = 7; set(r, a); return r.data[1]; }',
    'field_view_mismatch': FIELD + 'void set(inout R r, in I64[_] a) { r.data = a; }\n'
                           'export I64 run() { R r; I64[3] a; set(r, a); return 0; }',
    'indexed_field': FIELD + 'export I64 run() { R[2] rs; I64[2] a; a[1] = 7; rs[1].data = a; return rs[1].data[1]; }',
    'indexed_field_bounds': FIELD + STEP +
                            'export I64 run() { R[1] rs; R[2] other; I64[1] k; rs[k[0] + 1].data = other[step(k)].data; '
                            'return 0; }',
    'indexed_source_once': FIELD + STEP +
                           'export I64 run() { R[2] rs; rs[1].data[1] = 7; I64[1] k; k[0] = 1; I64[2] b; '
                           'b = rs[step(k)].data; return b[1] * 10 + k[0]; }',
    'source_effect_then_shape': FIELD + STEP +
                                'I64 hit = 0;\nI64 mark(inout I64[1] k) { hit += 1; return step(k); }\n'
                                'void put(inout I64[_] dst, inout I64[1] k) { R[2] rs; dst = rs[mark(k)].data; }\n'
                                'export I64 run() { I64[3] d; I64[1] k; put(d, k); return 0; }',
    'branch': MOVE + 'I64 pick(Bool yes) { I64[2] d; I64[3] s; if (yes) { move(d, s); } return 4; }\n'
              'export I64 run() { return pick(false); }',
    'loop': 'export I64 run() { I64[2] a; I64[2] b; I64 total = 0; for i in 0..3 { a[1] = i; b = a; '
            'total += b[1]; } return total; }',
    'initializer': 'export I64 run() { I64[2] a; a[1] = 7; I64[2] b = a; a[1] = 9; return b[1]; }',
    'initializer_view': 'I64 read(in I64[_] src) { I64[2] d = src; return d[1]; }\n'
                        'export I64 run() { I64[2] a; a[1] = 7; return read(a); }',
    'initializer_view_mismatch': 'I64 read(in I64[_] src) { I64[2] d = src; return d[1]; }\n'
                                 'export I64 run() { I64[1] a; return read(a); }',
    'initializer_empty': 'export I64 run() { I64[0] a; I64[0] b = a; return 3; }',
    'initializer_empty_mismatch': 'I64 read(in I64[_] src) { I64[0] d = src; return 3; }\n'
                                  'export I64 run() { I64[2] a; return read(a); }',
    'initializer_records': RECORD + 'export I64 run() { E[2] a; a[1].value = 7; E[2] b = a; return b[1].value; }',
    'initializer_field': FIELD + 'export I64 run() { R r; r.data[1] = 7; I64[2] b = r.data; return b[1]; }',
    'zero_records': ZERO + 'export I64 run() { Z[2] a; Z[2] b; b = a; return 4; }',
    'zero_record_views': ZERO + 'void move(inout Z[_] dst, in Z[_] src) { dst = src; }\n'
                         'export I64 run() { Z[2] d; Z[2] s; move(d, s); return 4; }',
    'zero_record_views_mismatch': ZERO + 'void move(inout Z[_] dst, in Z[_] src) { dst = src; }\n'
                                  'export I64 run() { Z[2] d; Z[3] s; move(d, s); return 0; }',
    'zero_record_field': ZERO + 'struct H { Z[2] data; I64 tag; }\n'
                         'export I64 run() { H h; Z[2] a; h.data = a; h.tag = 7; return h.tag; }',
    'zero_record_field_mismatch': ZERO + 'struct H { Z[2] data; I64 tag; }\n'
                                  'void set(inout H h, in Z[_] a) { h.data = a; }\n'
                                  'export I64 run() { H h; Z[1] a; set(h, a); return 0; }',
    'zero_record_initializer': ZERO + 'export I64 run() { Z[2] a; Z[2] b = a; return 5; }',
    'empty_field': 'struct G { I64[0] data; I64 tag; }\n'
                   'export I64 run() { G g; I64[0] a; g.data = a; g.tag = 3; return g.tag; }',
    'empty_field_mismatch': 'struct G { I64[0] data; I64 tag; }\nvoid set(inout G g, in I64[_] a) { g.data = a; }\n'
                            'export I64 run() { G g; I64[1] a; set(g, a); return 0; }',
    'initializer_loop': 'export I64 run() { I64[2] a; I64 total = 0; for i in 0..3 { a[1] = i; I64[2] b = a; '
                        'total += b[1]; } return total; }',
}
DIAGNOSTICS = {
    'static_mismatch': 'export I64 run() { I64[2] a; I64[3] b; a = b; return 0; }',
    'fixed_view_mismatch': 'void move(inout I64[2] dst, in I64[3] src) { dst = src; }\n'
                           'export I64 run() { return 0; }',
    'element_mismatch': 'export I64 run() { I64[2] a; U8[2] b; a = b; return 0; }',
    'nominal_mismatch': RECORD + 'struct F { I64 value; }\nexport I64 run() { E[2] a; F[2] b; a = b; return 0; }',
    'in_view_target': 'void move(in I64[_] dst, in I64[_] src) { dst = src; }\nexport I64 run() { return 0; }',
    'compound': 'export I64 run() { I64[2] a; I64[2] b; a += b; return 0; }',
    'scalar_source': 'export I64 run() { I64[2] a; a = 7; return 0; }',
    'initializer_static_mismatch': 'export I64 run() { I64[3] b; I64[2] a = b; return 0; }',
    'initializer_element_mismatch': 'export I64 run() { U8[2] b; I64[2] a = b; return 0; }',
    'initializer_self': 'export I64 run() { I64[2] a = a; return 0; }',
}

if __name__ == '__main__':
    raise SystemExit(runner.main(VALUES, DIAGNOSTICS, {},
        library='export struct E { I64 value; }\n', label='array assignment'))
