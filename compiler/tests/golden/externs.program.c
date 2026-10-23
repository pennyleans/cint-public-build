/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL extern const cint_module cg_7_externs_module;
CINT_RT_INTERNAL extern const cint_observer_entry cg_7_externs_obs[3];
CINT_RT_INTERNAL extern const cint_module_table cg_7_externs_table;

static const cint_module *const cg_modules[1] = {&cg_7_externs_module};
static const uint8_t cg_revision[32] = {
    0x14, 0x8b, 0x95, 0xb8, 0x43, 0x24, 0x0e, 0xe6,
    0x3e, 0x1b, 0x33, 0xdb, 0x46, 0x5f, 0xc1, 0x56,
    0x12, 0x73, 0xe1, 0xee, 0x91, 0xdc, 0x42, 0xee,
    0xc8, 0x73, 0x9e, 0xb2, 0xa7, 0xb3, 0xcc, 0x6e,
};
CINT_RT_INTERNAL const cint_program cg_program = {1u, 0u, cg_modules, cg_revision};

CINT_RT_EXPORT const cint_observer cint_observer_desc = {3u, 0u, &cg_program, cg_7_externs_obs};
CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
static const cint_module_table *const cg_tables[1] = {&cg_7_externs_table};
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 1u, cg_tables};
CINT_RT_INTERNAL extern const cint_host_table cg_7_externs_host;
static const cint_host_table *const cg_host[1] = {&cg_7_externs_host};
CINT_RT_EXPORT const cint_host_desc cint_imports_desc = {1u, 0u, cg_host};
