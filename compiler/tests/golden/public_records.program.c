/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL extern const cint_module cg_13_record_x5Flib_module;
CINT_RT_INTERNAL extern const cint_observer_entry cg_13_record_x5Flib_obs[1];
CINT_RT_INTERNAL extern const cint_module cg_13_record_x5Fmid_module;
CINT_RT_INTERNAL extern const cint_observer_entry cg_13_record_x5Fmid_obs[1];
CINT_RT_INTERNAL extern const cint_module cg_17_public_x5Frecords_module;
CINT_RT_INTERNAL extern const cint_observer_entry cg_17_public_x5Frecords_obs[1];
CINT_RT_INTERNAL extern const cint_module_table cg_13_record_x5Flib_table;
CINT_RT_INTERNAL extern const cint_module_table cg_13_record_x5Fmid_table;
CINT_RT_INTERNAL extern const cint_module_table cg_17_public_x5Frecords_table;

static const cint_module *const cg_modules[3] = {&cg_13_record_x5Flib_module, &cg_13_record_x5Fmid_module, &cg_17_public_x5Frecords_module};
static const uint8_t cg_revision[32] = {
    0x51, 0x1d, 0x35, 0xd5, 0xc3, 0xd6, 0x39, 0x41,
    0x76, 0x05, 0x05, 0x20, 0xc4, 0xd5, 0x8e, 0x33,
    0x17, 0x15, 0x3d, 0xad, 0x83, 0x75, 0x96, 0x96,
    0xb1, 0xb0, 0x12, 0x05, 0x79, 0x35, 0xc4, 0xbb,
};
CINT_RT_INTERNAL const cint_program cg_program = {3u, 0u, cg_modules, cg_revision};

CINT_RT_EXPORT const cint_observer cint_observer_desc = {1u, 0u, &cg_program, cg_17_public_x5Frecords_obs};
CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
static const cint_module_table *const cg_tables[3] = {&cg_13_record_x5Flib_table, &cg_13_record_x5Fmid_table, &cg_17_public_x5Frecords_table};
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 3u, cg_tables};
