/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL extern const cint_module cg_11_nest_x5Fcap_module;
CINT_RT_INTERNAL extern const cint_observer_entry cg_11_nest_x5Fcap_obs[1];
CINT_RT_INTERNAL extern const cint_module_table cg_11_nest_x5Fcap_table;

static const cint_module *const cg_modules[1] = {&cg_11_nest_x5Fcap_module};
static const uint8_t cg_revision[32] = {
    0x75, 0x91, 0x59, 0xc1, 0x41, 0x2e, 0x61, 0x63,
    0xaf, 0x12, 0x62, 0x59, 0x02, 0x47, 0x9b, 0x6b,
    0x93, 0xce, 0xe9, 0xd1, 0xfa, 0x73, 0x88, 0x21,
    0x9a, 0x40, 0xe7, 0xb8, 0x57, 0x71, 0xaa, 0x4c,
};
CINT_RT_INTERNAL const cint_program cg_program = {1u, 0u, cg_modules, cg_revision};

CINT_RT_EXPORT const cint_observer cint_observer_desc = {1u, 0u, &cg_program, cg_11_nest_x5Fcap_obs};
CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
static const cint_module_table *const cg_tables[1] = {&cg_11_nest_x5Fcap_table};
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 1u, cg_tables};
