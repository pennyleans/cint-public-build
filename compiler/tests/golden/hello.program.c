/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL extern const cint_module cg_5_hello_module;
CINT_RT_INTERNAL extern const cint_module_table cg_5_hello_table;
CINT_RT_INTERNAL extern cint_status cg_5_hello_run(cint_ctx *ctx, int64_t fuel);

static const cint_module *const cg_modules[1] = {&cg_5_hello_module};
static const uint8_t cg_revision[32] = {
    0x8a, 0xfd, 0x58, 0x72, 0x26, 0x9b, 0x92, 0x9a,
    0x53, 0x6a, 0xa6, 0x86, 0xbf, 0x1d, 0x15, 0x51,
    0x61, 0x29, 0x2f, 0x07, 0x1f, 0x66, 0x2e, 0xce,
    0xac, 0x43, 0x9c, 0x4a, 0x87, 0xdd, 0x82, 0x23,
};
CINT_RT_INTERNAL const cint_program cg_program = {1u, 0u, cg_modules, cg_revision};

CINT_RT_EXPORT const cint_observer cint_observer_desc = {0u, 0u, &cg_program, NULL};
CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
static const cint_module_table *const cg_tables[1] = {&cg_5_hello_table};
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 1u, cg_tables};

int main(int argc, char **argv)
{
    if (argc > 3) {
        return CINT_EXIT_USAGE;
    }
    return cint_program_run_fuel(&cg_program, cg_5_hello_run, CINT_FUEL_UNBOUNDED, argc >= 2 ? argv[1] : NULL,
                                 argc == 3 ? argv[2] : NULL);
}
