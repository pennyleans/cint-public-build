/* exercises private protocol readers and fault retention without injection hooks. */
#define main cint_cli_entry
#include "../../cint_main.c"
#include "../../cint_receipt.c"
#undef main

static const cint_site_info unit_sites[2] = {{0u, 0u, ""}, {3u, 13u, "add.checked.i64"}};
static const cint_module unit_module = {"unit_compiler.ci", 16u, 2u, unit_sites};
static const cint_module *const unit_modules[1] = {&unit_module};
static const cint_program unit_program = {1u, 0u, unit_modules, NULL};

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--decode") == 0) {
        uint8_t *bytes;
        size_t count;
        frec *record = calloc(1, sizeof *record);
        sbuf rendered = {0};
        if (record == NULL || read_file(argv[2], &bytes, &count) != 0) {
            return 5;
        }
        if (decode_fault(bytes, count, record, atoi(argv[3])) != 0) {
            free(record);
            free(bytes);
            return 7;
        }
        sb_jfault(&rendered, record, -1);
        sb_s(&rendered, "\n");
        fwrite(rendered.p, 1, rendered.n, stdout);
        free(rendered.p);
        free(record);
        free(bytes);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--internal") == 0) {
        cint_ctx_config config;
        cint_site site = {0u, 1u};
        int64_t result = 0;
        int status;
        memset(&config, 0, sizeof config);
        config.size = (uint32_t)sizeof config;
        config.program = &unit_program;
        if (cint_ctx_create(&config, &g.ctx) != CINT_OK || cli_mkdirs(argv[2]) != 0) {
            return 5;
        }
        snprintf(g.cachedir, sizeof g.cachedir, "%s", argv[2]);
        snprintf(g.rel, sizeof g.rel, "unit_input.ci");
        json_mode = 1;
        if (cint_rt_entry_begin(g.ctx, site, CINT_FUEL_UNBOUNDED, CINT_DEPTH_FROM_CONFIG) != CINT_OK) {
            cint_ctx_destroy(g.ctx);
            return 5;
        }
        (void)cint_add_i64(g.ctx, site, INT64_MAX, 1, &result);
        if (cint_rt_entry_end(g.ctx) != CINT_FAULT) {
            cint_ctx_destroy(g.ctx);
            return 5;
        }
        status = internal_compiler_fault("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        cint_ctx_destroy(g.ctx);
        return status;
    }
    return 4;
}
