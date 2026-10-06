#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

#define CX(name) cx_16_record_x5Farrays_##name
extern const cint_module_info cm_16_record_x5Farrays;
cint_status CX(4_echo)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(5_empty)(cint_ctx *, int64_t, void *);
cint_status CX(4_bits)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(6_packet)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(5_touch)(cint_ctx *, int64_t, cint_view, int64_t *);
cint_status CX(6_change)(cint_ctx *, int64_t, cint_view, int64_t, void *);

typedef struct { uint8_t active; int64_t value; uint8_t bits[2]; int64_t numbers[2]; } Row;
typedef struct { Row rows[2]; uint8_t tail; } Grid;
typedef struct { Grid grids[2]; Row extra; uint8_t done; } Frame;
typedef struct { uint8_t active; } Bit;
typedef struct { Bit items[257]; uint8_t tail; } Bits;
typedef struct { uint8_t flags[2]; int64_t values[2]; } Item;
typedef struct { Item items[2]; uint8_t tail; } Packet;

static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static cint_view view(cint_buffer_id buffer, uint32_t record)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = buffer; v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD; v.type.record_id = record;
    v.rank = 1; v.perm = CINT_VIEW_WRITE; v.shape[0] = 1; v.stride[0] = 1;
    return v;
}
static int refused(cint_ctx *ctx, cint_status status, uint32_t expected, int64_t prior_fuel)
{
    uint32_t reason = 0;
    int64_t fuel = -1;
    return status == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK && reason == expected &&
           cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == prior_fuel;
}
static int faulted(cint_ctx *ctx, cint_status status, uint16_t code, const char *op)
{
    static cint_fault_record rec;
    int64_t fuel = -1;
    return status == CINT_FAULT && cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == code &&
           rec.operation_len == strlen(op) && memcmp(rec.operation, op, rec.operation_len) == 0 &&
           cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == 0 && cint_ctx_clear_fault(ctx) == CINT_OK;
}
static size_t row_bools(size_t *offsets, size_t n, size_t base)
{
    offsets[n++] = base + offsetof(Row, active);
    offsets[n++] = base + offsetof(Row, bits);
    offsets[n++] = base + offsetof(Row, bits) + 1u;
    return n;
}
int main(void)
{
    const cint_module_info *cm = &cm_16_record_x5Farrays;
    const cint_record_layout *r = cm->records;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_buffer_id fi = 0, bi = 0, pi = 0;
    cint_view iv, biv, piv;
    Frame input, output, before;
    Bits bits_in, bits_out, bits_before;
    Packet packet_in, packet_out, packet_before;
    size_t offsets[18], count = 0;
    int64_t result = 99, prior_fuel = -1;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config; config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete source-order catalog", cm->record_count == 12u);
    if (cm->record_count != 12u) { cint_ctx_destroy(ctx); return 1; }
    check("imported array layouts", r[9].bytes == sizeof(Item) && r[10].bytes == sizeof(Packet) &&
          r[9].fields[0].count == 2u && r[9].fields[1].count == 2u &&
          r[10].fields[0].count == 2u && r[10].fields[0].type.record_id == 9u &&
          r[10].fields[1].offset == offsetof(Packet, tail));
    check("forward record-array layout", r[0].bytes == sizeof(Frame) && r[0].align == CINT_ELEM_ALIGN(sizeof(Frame)) &&
          r[0].fields[0].count == 2u && r[0].fields[0].type.record_id == 1u &&
          r[0].fields[1].type.record_id == 2u && r[0].fields[1].offset == offsetof(Frame, extra) &&
          r[0].fields[2].offset == offsetof(Frame, done));
    check("nested field layout", r[1].bytes == sizeof(Grid) && r[1].fields[0].count == 2u &&
          r[1].fields[0].type.record_id == 2u && r[1].fields[1].offset == offsetof(Grid, tail) &&
          r[2].bytes == sizeof(Row) && r[2].fields[2].offset == offsetof(Row, bits) &&
          r[2].fields[2].count == 2u && r[2].fields[3].count == 2u);
    check("large cursor and qualified field layout", r[5].bytes == sizeof(Bits) &&
          r[5].fields[0].count == 257u && r[5].fields[0].type.record_id == 6u &&
          r[5].fields[1].offset == offsetof(Bits, tail) && r[7].fields[0].type.record_id == 11u &&
          r[8].bytes == sizeof(uint16_t));
    for (size_t g = 0; g < 2u; g++) {
        for (size_t j = 0; j < 2u; j++) {
            count = row_bools(offsets, count, offsetof(Frame, grids) + g * sizeof(Grid) + j * sizeof(Row));
        }
        offsets[count++] = offsetof(Frame, grids) + g * sizeof(Grid) + offsetof(Grid, tail);
    }
    count = row_bools(offsets, count, offsetof(Frame, extra));
    offsets[count++] = offsetof(Frame, done);
    check("independent bool offset count", count == 18u);
    memset(&input, 0, sizeof input); memset(&output, 0xA5, sizeof output);
    memcpy(&before, &output, sizeof before);
    memset(&bits_in, 0, sizeof bits_in); memset(&bits_out, 0xA5, sizeof bits_out);
    memcpy(&bits_before, &bits_out, sizeof bits_before);
    memset(&packet_in, 0, sizeof packet_in); memset(&packet_out, 0xA5, sizeof packet_out);
    memcpy(&packet_before, &packet_out, sizeof packet_before);
    check("registry", cint_buffer_register_bytes(ctx, &input, sizeof input, CINT_VIEW_WRITE, &fi) == CINT_OK &&
          cint_buffer_register_bytes(ctx, &bits_in, sizeof bits_in, CINT_VIEW_WRITE, &bi) == CINT_OK &&
          cint_buffer_register_bytes(ctx, &packet_in, sizeof packet_in, CINT_VIEW_WRITE, &pi) == CINT_OK);
    iv = view(fi, 0); biv = view(bi, 5); piv = view(pi, 10);
    iv.perm = CINT_VIEW_READ;
    for (size_t j = 0; j < count; j++) {
        unsigned char *bytes = (unsigned char *)&input;
        bytes[offsets[j]] = 2;
        check("every nested incoming bool", cint_fuel_consumed(ctx, &prior_fuel) == CINT_OK &&
              refused(ctx, CX(4_echo)(ctx, -1, iv, &output), CINT_REFUSAL_BOOL, prior_fuel) &&
              memcmp(&output, &before, sizeof output) == 0);
        bytes[offsets[j]] = 0;
    }
    input.grids[1].rows[1].active = 1; input.grids[1].rows[1].bits[1] = 1;
    input.grids[1].rows[1].value = 8;
    check("readonly nested copy", CX(4_echo)(ctx, -1, iv, &output) == CINT_OK &&
          output.grids[1].rows[1].value == 8 && output.grids[1].rows[1].bits[1] == 1);
    check("readonly view faults", faulted(ctx, CX(5_touch)(ctx, -1, iv, &result), CINT_E_ALIAS, "bind.permission") &&
          result == 99);
    iv.perm = CINT_VIEW_WRITE;
    check("nested view mutation", CX(5_touch)(ctx, -1, iv, &result) == CINT_OK && result == 9 &&
          input.grids[1].rows[1].value == 9);
    input.grids[1].rows[1].bits[1] = 2;
    check("nested view bool refused", cint_fuel_consumed(ctx, &prior_fuel) == CINT_OK &&
          refused(ctx, CX(5_touch)(ctx, -1, iv, &result), CINT_REFUSAL_BOOL, prior_fuel) && result == 9);
    input.grids[1].rows[1].bits[1] = 1;
    memcpy(&output, &before, sizeof output);
    check("fault preserves earlier store and output", CX(6_change)(ctx, -1, iv, 0, &output) == CINT_FAULT &&
          input.grids[1].rows[1].value == 33 && memcmp(&output, &before, sizeof output) == 0);
    check("clear fault", cint_ctx_clear_fault(ctx) == CINT_OK);
    check("record-array result publication", CX(6_change)(ctx, -1, iv, 2, &output) == CINT_OK &&
          output.extra.value == 5 && output.grids[1].rows[1].value == 33 && output.grids[1].rows[1].bits[1] == 1);
    check("record-array result zero", CX(5_empty)(ctx, -1, &output) == CINT_OK &&
          output.grids[1].rows[1].value == 0 && output.extra.numbers[1] == 0 && output.done == 0);
    for (size_t j = 0; j < count; j++) {
        check("every nested result bool zero", ((const unsigned char *)&output)[offsets[j]] == 0);
    }
    bits_in.items[256].active = 2;
    check("bool cursor above 255", cint_fuel_consumed(ctx, &prior_fuel) == CINT_OK &&
          refused(ctx, CX(4_bits)(ctx, -1, biv, &bits_out), CINT_REFUSAL_BOOL, prior_fuel) &&
          memcmp(&bits_out, &bits_before, sizeof bits_out) == 0);
    bits_in.items[256].active = 1; bits_in.tail = 2;
    check("bool cursor restores parent", cint_fuel_consumed(ctx, &prior_fuel) == CINT_OK &&
          refused(ctx, CX(4_bits)(ctx, -1, biv, &bits_out), CINT_REFUSAL_BOOL, prior_fuel));
    bits_in.tail = 1;
    check("long record-array copy", CX(4_bits)(ctx, -1, biv, &bits_out) == CINT_OK &&
          bits_out.items[256].active == 1 && bits_out.tail == 1 && bits_out.items[0].active == 0);
    packet_in.items[1].flags[1] = 2;
    check("imported scalar-array bool", cint_fuel_consumed(ctx, &prior_fuel) == CINT_OK &&
          refused(ctx, CX(6_packet)(ctx, -1, piv, &packet_out), CINT_REFUSAL_BOOL, prior_fuel) &&
          memcmp(&packet_out, &packet_before, sizeof packet_out) == 0);
    packet_in.items[1].flags[1] = 1; packet_in.items[1].values[1] = 41; packet_in.tail = 1;
    check("imported array-field copy", CX(6_packet)(ctx, -1, piv, &packet_out) == CINT_OK &&
          packet_out.items[1].flags[1] == 1 && packet_out.items[1].values[1] == 41 && packet_out.tail == 1);
    cint_ctx_destroy(ctx);
    printf("record arrays public: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
