/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL extern const cint_module cg_15_public_x5Fviews_module;
CINT_RT_INTERNAL extern const cint_module_table cg_15_public_x5Fviews_table;

static const cint_module *const cg_modules[1] = {&cg_15_public_x5Fviews_module};
static const uint8_t cg_revision[32] = {
    0xac, 0x04, 0x63, 0x59, 0xbd, 0x27, 0x9d, 0x14,
    0x26, 0xee, 0xce, 0xc4, 0x4b, 0x51, 0x1a, 0x1e,
    0xb8, 0x19, 0xdb, 0x4c, 0xc3, 0xc4, 0xa8, 0xef,
    0xee, 0x7e, 0x86, 0x3c, 0xac, 0x4c, 0xd5, 0x42,
};
CINT_RT_INTERNAL const cint_program cg_program = {1u, 0u, cg_modules, cg_revision};

CINT_RT_EXPORT const cint_observer cint_observer_desc = {0u, 0u, &cg_program, NULL};
CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
static const cint_module_table *const cg_tables[1] = {&cg_15_public_x5Fviews_table};
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 1u, cg_tables};
