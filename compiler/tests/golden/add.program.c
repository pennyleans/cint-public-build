/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL extern const cint_module cg_3_add_module;
CINT_RT_INTERNAL extern const cint_observer_entry cg_3_add_obs[1];
CINT_RT_INTERNAL extern const cint_module_table cg_3_add_table;

static const cint_module *const cg_modules[1] = {&cg_3_add_module};
static const uint8_t cg_revision[32] = {
    0xfb, 0x64, 0xa1, 0x67, 0x54, 0xf0, 0x9a, 0xe9,
    0x4c, 0x9f, 0x67, 0x44, 0xb8, 0x8d, 0xe6, 0x79,
    0x1c, 0xf5, 0xa9, 0xdc, 0x43, 0x28, 0xe2, 0xc8,
    0xa8, 0x82, 0xff, 0x36, 0xce, 0xf5, 0xc3, 0xe8,
};
CINT_RT_INTERNAL const cint_program cg_program = {1u, 0u, cg_modules, cg_revision};

CINT_RT_EXPORT const cint_observer cint_observer_desc = {1u, 0u, &cg_program, cg_3_add_obs};
CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
static const cint_module_table *const cg_tables[1] = {&cg_3_add_table};
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 1u, cg_tables};
