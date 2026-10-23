/* rt/cint_program.c: the body of a generated program's C main (SPEC-06 3.2, 3.4a; D-19),
 * included by rt/cint_rt.c inside the `cint_rt` library and counted in its own SPEC-09 4.2
 * row (I/O services lane U5, rt/OPEN.md RT-OQ-74). cint_program_run and its two variants
 * take the destinations as arguments; cint_program_run_args takes a C main's argv, with
 * the `--grant` lines of `cint run --grant` (OQ-300) and the program's host import tables,
 * so that a program's service calls (rt/cint_fs.c, rt/cint_console.c) resolve and are
 * granted. Everything here runs in the program's own process, never in the `cint`
 * command's. */


static cint_status write_stdout(void *user, const uint8_t *bytes, size_t len)
{
    if (len > 0u && fwrite(bytes, 1u, len, stdout) != len) {
        *(bool *)user = true;
        return CINT_RESOURCE;
    }
    return CINT_OK;
}

/* "handle:<N>" sets *handle and returns 1; a nonempty path returns 2; NULL
 * returns 0; anything else -1. */
static int fault_dest_kind(const char *dest, uint64_t *handle)
{
    static const char prefix[] = "handle:";
    const char *p;
    uint64_t v = 0u;
#if defined(_WIN32)
    const uint64_t max = (uint64_t)INTPTR_MAX;
#else
    const uint64_t max = (uint64_t)INT_MAX;
#endif
    if (dest == NULL) return 0;
    if (strncmp(dest, prefix, sizeof prefix - 1u) != 0) return dest[0] != '\0' ? 2 : -1;
    p = dest + (sizeof prefix - 1u);
    if (*p == '\0') return -1;
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9' || v > (max - (uint64_t)(*p - '0')) / 10u) return -1;
        v = v * 10u + (uint64_t)(*p - '0');
    }
    *handle = v;
    return 1;
}

#if defined(_WIN32)
static bool write_handle(HANDLE h, const uint8_t *p, size_t n)
{
    while (n > 0u) {
        DWORD chunk = n > 0x40000000u ? 0x40000000u : (DWORD)n, done = 0;
        if (!WriteFile(h, p, chunk, &done, NULL) || done == 0u) return false;
        p += done;
        n -= done;
    }
    return true;
}

static bool write_record(int kind, const char *path, uint64_t handle, const uint8_t *bytes, size_t len)
{
    HANDLE h;
    wchar_t *wide;
    int wn;
    bool ok;
    if (kind == 1) return write_handle((HANDLE)(uintptr_t)handle, bytes, len);
    wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (wn <= 0) return false;
    wide = (wchar_t *)malloc((size_t)wn * sizeof *wide);
    if (wide == NULL) return false;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, wn) != wn) {
        free(wide);
        return false;
    }
    h = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (h == INVALID_HANDLE_VALUE) return false;
    ok = write_handle(h, bytes, len);
    return CloseHandle(h) != 0 && ok;
}
#else
static bool write_fd(int fd, const uint8_t *p, size_t n)
{
    while (n > 0u) {
        ssize_t done = write(fd, p, n);
        if (done < 0 && errno == EINTR) continue;
        if (done <= 0) return false;
        p += done;
        n -= (size_t)done;
    }
    return true;
}

static bool write_record(int kind, const char *path, uint64_t handle, const uint8_t *bytes, size_t len)
{
    int fd;
    bool ok;
    if (kind == 1) return write_fd((int)handle, bytes, len);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    ok = write_fd(fd, bytes, len);
    return close(fd) == 0 && ok;
}
#endif

/* Bytes and decimals to stderr, for the lines below. The printf family is not
 * used: MSVC's C runtime defines fprintf and its helpers inline, so the
 * runtime object would define symbols outside cint_ (SPEC-09 EMIT-31). */
static void err_put(const char *p, size_t n)
{
    if (n > 0u) (void)fwrite(p, 1u, n, stderr);
}

static void err_str(const char *s)
{
    err_put(s, strlen(s));
}

static void err_u64(uint64_t v)
{
    char d[20];
    size_t n = 0u;
    do {
        d[sizeof d - 1u - n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    err_put(d + sizeof d - n, n);
}

/* The width report (games G3; rt/OPEN.md RT-OQ-64; cint_wide.h section 2).
 * ctx->width holds the module count plus one offsets, then two words per site
 * of every module: its hits, and the tag of its operands above the largest
 * width, sign bit included, that a value at the site reached. */
void cint_rt_width_note(cint_ctx *ctx, cint_site site, uint32_t tag, uint32_t hit, const uint64_t *a,
                        const uint64_t *b, const uint64_t *c)
{
    const uint64_t *v[3] = {a, b, c};
    unsigned n = cint_wide_limbs(tag);
    uint64_t *row, need = 0u;
    if (ctx == NULL || ctx->width == NULL || site.module >= ctx->program->module_count ||
        site.index >= ctx->program->modules[site.module]->site_count) {
        return;
    }
    row = ctx->width + ctx->program->module_count + 1u + 2u * (ctx->width[site.module] + site.index);
    if (hit != 0u) {
        row[0]++;
        if ((row[1] >> 32) == 0u) row[1] |= (uint64_t)tag << 32;
    }
    for (unsigned k = 0u; k < 3u && n != 0u; k++) {
        uint64_t flip = v[k] != NULL && cint_rt_tag_signed(tag) && (v[k][n - 1u] >> 63) != 0u ? UINT64_MAX : 0u, top;
        unsigned i = n, bits = 1u;
        if (v[k] == NULL) continue;
        while (i > 0u && (v[k][i - 1u] ^ flip) == 0u) i--;
        if (i > 0u) {
            for (top = v[k][i - 1u] ^ flip, bits += 64u * (i - 1u); top != 0u; top >>= 1) bits++;
        }
        if (bits > need) need = bits;
    }
    if (need > (row[1] & UINT32_MAX)) row[1] = (row[1] & ~(uint64_t)UINT32_MAX) | need;
}

/* Whether the hooks of cint_wide.h see the operation of a site: one with a
 * wide operand type (either side for `as`) among the families they cover. */
static bool width_op(const char *op)
{
    static const char *const names[] = {"add", "sub", "mul", "neg", "div", "rem", "shl", "shr", "abs", "clamp",
                                        "div_trunc", "rem_trunc", "div_euclid", "rem_euclid", "div_round", "isqrt",
                                        "isqrt_round", "muldiv", "rotl", "rotr", "as"};
    static const char *const wide[] = {"i128", "i256", "i512", "i1024", "u128", "u256", "u512"};
    const char *dot = strchr(op, '.'), *t;
    size_t len = dot != NULL ? (size_t)(dot - op) : 0u, i, k;
    bool known = false, as = len == 2u && strncmp(op, "as", 2u) == 0;
    for (i = 0u; i < sizeof names / sizeof names[0]; i++) known = known || (strlen(names[i]) == len && strncmp(op, names[i], len) == 0);
    if (!known || (strncmp(dot, ".checked.", 9u) != 0 && strncmp(op, "shl.wrap.", 9u) != 0)) return false;
    for (t = dot + 9 - (dot[1] == 'w' ? 3 : 0), k = 0u; k < (as ? 2u : 1u) && *t != '\0'; k++) {
        const char *end = strchr(t, '.');
        size_t n = end != NULL ? (size_t)(end - t) : strlen(t);
        for (i = 0u; i < sizeof wide / sizeof wide[0]; i++) {
            if (strlen(wide[i]) == n && strncmp(t, wide[i], n) == 0) return true;
        }
        t += n + (end != NULL ? 1u : 0u);
    }
    return false;
}

/* The report to stderr: CINT-WIDTH-1, a line per site in program order that
 * was reached (`site`, with its hits, the largest magnitude in bits of its
 * type, and the narrowest type that holds every value it saw) or that the
 * hooks see and was not (`unreached`), then `end`. */
static bool width_report(const cint_ctx *ctx)
{
    static const char *const fits[2][8] = {{"U8", "U16", "U32", "U64", "U128", "U256", "U512", "U1024"},
                                           {"I8", "I16", "I32", "I64", "I128", "I256", "I512", "I1024"}};
    const cint_program *p = ctx->program;
    err_str("CINT-WIDTH-1\n");
    for (uint32_t m = 0u; m < p->module_count; m++) {
        const cint_module *mod = p->modules[m];
        for (uint32_t s = 1u; s < mod->site_count; s++) {
            const uint64_t *row = ctx->width + p->module_count + 1u + 2u * (ctx->width[m] + s);
            uint32_t tag = (uint32_t)(row[1] >> 32), sign = cint_rt_tag_signed(tag) ? 1u : 0u, k = 0u;
            uint64_t bits = (row[1] & UINT32_MAX) - (sign == 0u && (row[1] & UINT32_MAX) > 0u ? 1u : 0u);
            if (row[0] == 0u && !width_op(mod->sites[s].operation)) continue;
            err_str(row[0] != 0u ? "site " : "unreached ");
            err_put(mod->path, mod->path_len);
            err_str(":");
            err_u64(mod->sites[s].line);
            err_str(":");
            err_u64(mod->sites[s].column);
            err_str(" ");
            err_str(mod->sites[s].operation);
            if (row[0] != 0u) {
                while (k < 7u && (8u << k) < bits) k++;
                err_str(" hits ");
                err_u64(row[0]);
                err_str(" bits ");
                err_u64(bits);
                err_str(" of ");
                err_u64(cint_wide_limbs(tag) * 64u);
                err_str(" fits ");
                err_str(fits[sign][k]);
            }
            err_str("\n");
        }
    }
    err_str("end\n");
    return fflush(stderr) == 0 && ferror(stderr) == 0;
}

/* Without a fault destination (decision 2026-10-14, OQ-289 A; SPEC-06 3.4a):
 * one line on stderr naming the fault code, the operation and the position,
 * and where the full record goes. The record encoded, so the position
 * resolves. Whether stderr takes the line does not change the exit status. */
static void fault_line(const cint_fault_record *r)
{
    static const char *const names[] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT",
                                        "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED",
                                        "E_DOMAIN", "E_DEPTH", "E_ASSERT"};
    cint_position pos;
    if (!cint_site_resolve(r->program, r->position, &pos)) return;
    err_str("fault[");
    if (r->code < sizeof names / sizeof names[0]) err_str(names[r->code]);
    else err_u64(r->code);
    err_str("]: ");
    err_put(r->operation, r->operation_len);
    err_str(" at ");
    err_put(pos.path, pos.path_len);
    err_str(":");
    err_u64(pos.line);
    err_str(":");
    err_u64(pos.column);
    err_str(" (pass a file path as argv[1] for the full fault record)\n");
    (void)fflush(stderr);
}

static int report_fault(const cint_ctx *ctx, int kind, const char *dest, uint64_t handle)
{
    size_t len = cint_fault_encode(&ctx->fault, NULL, 0u);
    uint8_t *bytes;
    bool ok;
    if (len == 0u) return CINT_EXIT_INTERNAL;
    if (kind == 0) {
        fault_line(&ctx->fault);
        return CINT_EXIT_FAULT;
    }
    bytes = (uint8_t *)malloc(len);
    if (bytes == NULL) return CINT_EXIT_ENVIRONMENT;
    (void)cint_fault_encode(&ctx->fault, bytes, len);
    ok = write_record(kind, dest, handle, bytes, len);
    free(bytes);
    return ok ? CINT_EXIT_FAULT : CINT_EXIT_ENVIRONMENT;
}

/* The fuel record (CINT_FUEL_RECORD_DOMAIN, then fuel consumed as I64,
 * little-endian, SPEC-01 IM-144) to the destination of kind `kind`. */
static bool report_fuel(const cint_ctx *ctx, int kind, const char *dest, uint64_t handle)
{
    static const char domain[] = CINT_FUEL_RECORD_DOMAIN;
    uint8_t bytes[CINT_FUEL_RECORD_BYTES];
    uint64_t used = (uint64_t)ctx->head.fuel_used, n = sizeof domain - 1u;
    size_t i;
    /* Shift the value itself: MSVC /O2 vectorized `x >> (8u * i)` with pooled
     * __xmm@ constants, which the EMIT-31 audit of the runtime object rejects. */
    for (i = 0; i < 4u; i++, n >>= 8u) bytes[i] = (uint8_t)n;
    memcpy(bytes + 4, domain, sizeof domain - 1u);
    for (i = 0; i < 8u; i++, used >>= 8u) bytes[4u + (sizeof domain - 1u) + i] = (uint8_t)used;
    return write_record(kind, dest, handle, bytes, sizeof bytes);
}

/* n little-endian bytes of v at p; returns p + n. The value itself is shifted,
 * as in report_fuel. */
static uint8_t *put_u(uint8_t *p, uint64_t v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++, v >>= 8u) p[i] = (uint8_t)v;
    return p + n;
}

/* The error record (cint_rt.h 6d; RT-OQ-33) of the context's error result. */
static int report_error(const cint_ctx *ctx, int kind, const char *dest, uint64_t handle)
{
    static const char domain[] = CINT_ERROR_RECORD_DOMAIN;
    const cint_error_set *s = ctx->error_set;
    const char *value = s->values != NULL ? s->values[ctx->error_tag - 1u] : NULL;
    size_t nlen = s->name != NULL ? strlen(s->name) : 0u, vlen = value != NULL ? strlen(value) : 0u, len;
    uint8_t *bytes, *p;
    bool ok;
    if (nlen == 0u || vlen == 0u || nlen > UINT32_MAX || vlen > UINT32_MAX || s->tag < CINT_TAG_U8 ||
        s->tag > CINT_TAG_U64 || ctx->error_tag > cint_rt_mask(cint_rt_tag_width(s->tag))) {
        return CINT_EXIT_INTERNAL;
    }
    if (kind == 0) {
        /* The line `cint run` prints (SPEC-06 3.4a; OQ-289 A). */
        err_str("error: ");
        err_put(s->name, nlen);
        err_str(".");
        err_put(value, vlen);
        err_str(" (tag ");
        err_u64(ctx->error_tag);
        err_str(")\n");
        (void)fflush(stderr);
        return CINT_EXIT_ERROR_VALUE;
    }
    len = 4u + (sizeof domain - 1u) + 4u + nlen + 4u + vlen + 4u + 8u;
    bytes = (uint8_t *)malloc(len);
    if (bytes == NULL) return CINT_EXIT_ENVIRONMENT;
    p = put_u(bytes, sizeof domain - 1u, 4u);
    memcpy(p, domain, sizeof domain - 1u);
    p = put_u(p + (sizeof domain - 1u), nlen, 4u);
    memcpy(p, s->name, nlen);
    p = put_u(p + nlen, vlen, 4u);
    memcpy(p, value, vlen);
    p = put_u(p + vlen, s->tag, 4u);
    (void)put_u(p, ctx->error_tag, 8u);
    ok = write_record(kind, dest, handle, bytes, len);
    free(bytes);
    return ok ? CINT_EXIT_ERROR_VALUE : CINT_EXIT_ENVIRONMENT;
}

/* The `state.global` lines of `t` (cint_state_render) to the destination of
 * kind `kind`: CINT_EXIT_OK, CINT_EXIT_ENVIRONMENT when they cannot be
 * written, or CINT_EXIT_INTERNAL for a malformed table. */
static int report_state(const cint_ctx *ctx, const cint_state_table *t, int kind, const char *dest,
                        uint64_t handle)
{
    uint64_t cap = 1u;
    size_t n;
    char *text;
    bool ok;
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states != NULL ? t->states[i] : NULL;
        if (s == NULL || (s->var_count > 0u && s->vars == NULL)) return CINT_EXIT_INTERNAL;
        for (uint32_t j = 0u; j < s->var_count; j++) {
            /* A line is at most 48 bytes besides its module path and name. */
            cap += 48u + CINT_FAULT_MAX_PATH + s->vars[j].name_len;
            if (cap > PRINT_MAX) return CINT_EXIT_INTERNAL;
        }
    }
    text = (char *)malloc((size_t)cap);
    if (text == NULL) return CINT_EXIT_ENVIRONMENT;
    n = cint_state_render(ctx, t, text, (size_t)cap);
    ok = n != SIZE_MAX && write_record(kind, dest, handle, (const uint8_t *)text, n);
    free(text);
    return n == SIZE_MAX ? CINT_EXIT_INTERNAL : ok ? CINT_EXIT_OK : CINT_EXIT_ENVIRONMENT;
}

int cint_program_run(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest)
{
    return cint_program_run_state(program, main_fn, fuel, fault_dest, NULL, NULL, NULL);
}

int cint_program_run_fuel(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                          const char *fuel_dest)
{
    return cint_program_run_state(program, main_fn, fuel, fault_dest, fuel_dest, NULL, NULL);
}

static int program_run(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                       const char *fuel_dest, const char *state_dest, const cint_state_table *state, bool width,
                       const cint_host_desc *imports, const uint8_t *grants, size_t grants_length,
                       const uint8_t *args, size_t args_length);

int cint_program_run_state(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                           const char *fuel_dest, const char *state_dest, const cint_state_table *state)
{
    return program_run(program, main_fn, fuel, fault_dest, fuel_dest, state_dest, state, false, NULL, NULL, 0u, NULL, 0u);
}

/* The width report's entry points (cint_rt_internal.h). */
int cint_program_run_width(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                           const char *fuel_dest)
{
    return program_run(program, main_fn, fuel, fault_dest, fuel_dest, NULL, NULL, true, NULL, NULL, 0u, NULL, 0u);
}

int cint_program_run_width_state(const cint_program *program, cint_main_fn main_fn, int64_t fuel,
                                 const char *fault_dest, const char *fuel_dest, const char *state_dest,
                                 const cint_state_table *state)
{
    return program_run(program, main_fn, fuel, fault_dest, fuel_dest, state_dest, state, true, NULL, NULL, 0u, NULL, 0u);
}

static int program_run(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                       const char *fuel_dest, const char *state_dest, const cint_state_table *state, bool width,
                       const cint_host_desc *imports, const uint8_t *grants, size_t grants_length,
                       const uint8_t *args, size_t args_length)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_status st;
    uint64_t handle = 0u, fuel_handle = 0u, state_handle = 0u;
    bool out_failed = false, ran;
    int kind = fault_dest_kind(fault_dest, &handle), fuel_kind = fault_dest_kind(fuel_dest, &fuel_handle);
    int state_kind = fault_dest_kind(state_dest, &state_handle), status = CINT_EXIT_OK;
    if (kind < 0 || fuel_kind < 0 || state_kind < 0) return CINT_EXIT_USAGE;
    if (program == NULL || main_fn == NULL) return CINT_EXIT_INTERNAL;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return CINT_EXIT_ENVIRONMENT;
    if (width && _setmode(_fileno(stderr), _O_BINARY) == -1) return CINT_EXIT_ENVIRONMENT;  /* LF lines */
#endif
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = program;
    cfg.output = write_stdout;
    cfg.output_user = &out_failed;
    cfg.imports = imports;
    cfg.grants = grants;
    cfg.grants_length = grants_length;
    cfg.args = args;
    cfg.args_length = args_length;
    st = cint_ctx_create(&cfg, &ctx);
    if (st == CINT_REFUSED && (imports != NULL || grants != NULL)) {
        /* A grant line the runtime does not take, or a host import that does not resolve
         * (rt/cint_host.c, rt/cint_fs.c): a usage error of the program's command line. */
        err_str("usage: a --grant line is malformed or names a directory that cannot be opened, or the "
                "program's host imports do not resolve\n");
        (void)fflush(stderr);
        return CINT_EXIT_USAGE;
    }
    if (st != CINT_OK) return st == CINT_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_INTERNAL;
    if (width) {
        uint64_t rows = 0u;
        for (uint32_t m = 0u; m < program->module_count; m++) rows += program->modules[m]->site_count;
        if ((ctx->width = (uint64_t *)calloc(program->module_count + 1u + 2u * rows, sizeof(uint64_t))) == NULL) {
            cint_ctx_destroy(ctx);
            return CINT_EXIT_ENVIRONMENT;
        }
        for (uint32_t m = 0u; m < program->module_count; m++) {
            ctx->width[m + 1u] = ctx->width[m] + program->modules[m]->site_count;
        }
    }
    st = main_fn(ctx, fuel);
    /* stdout is flushed before a fault is reported and before exit (3.4a). */
    if (fflush(stdout) != 0 || ferror(stdout) != 0) out_failed = true;
    ran = st == CINT_OK || (st == CINT_FAULT && ctx->head.faulted != 0u);
    if (!out_failed && ran && width && !width_report(ctx)) out_failed = true;
    free(ctx->width);
    ctx->width = NULL;
    if (!out_failed && ran && fuel_kind != 0 && !report_fuel(ctx, fuel_kind, fuel_dest, fuel_handle)) {
        out_failed = true;  /* the fuel record could not be written: an environment failure */
    }
    if (!out_failed && ran && state_kind != 0 && state != NULL) {
        status = report_state(ctx, state, state_kind, state_dest, state_handle);
        out_failed = status == CINT_EXIT_ENVIRONMENT;
    }
    if (out_failed) {
        status = CINT_EXIT_ENVIRONMENT;
    } else if (status != CINT_EXIT_OK) {
        /* the state table is malformed: CINT_EXIT_INTERNAL */
    } else if (st == CINT_OK) {
        status = ctx->error_set != NULL ? report_error(ctx, kind, fault_dest, handle) : CINT_EXIT_OK;
    } else if (st == CINT_FAULT && ctx->head.faulted != 0u) {
        status = report_fault(ctx, kind, fault_dest, handle);
    } else {
        status = CINT_EXIT_INTERNAL;
    }
    cint_ctx_destroy(ctx);
    return status;
}

/* cint_program_run_args and cint_program_run_argv (Proposed, rt/OPEN.md RT-OQ-74 and RT-OQ-76; I/O
 * services lane U5 and U6): the body of a generated program's C main, from its argv. "--grant"
 * followed by one line, anywhere and any number of times, is a grant line of SPEC-07 SEC-CAP-6
 * (rt/cint_fs.c: `<text>` or `<text>=<host dir>`); "--arg" followed by one argument, anywhere and
 * any number of times, and every argument after "--", are the program's arguments (OQ-306);
 * every other argument after argv[0] is a destination, in order the fault, fuel and state
 * destinations of cint_program_run_state, "-" for none. A third destination without `state`, a
 * fourth, a "--grant" with no line or an empty line after it, a "--arg" with nothing after it,
 * or a program argument when the program's `main` takes none (CINT_PROGRAM_ARGS is not in
 * `flags`) returns CINT_EXIT_USAGE and runs nothing. The lines, joined by LF, are the context's
 * grants, the arguments, each followed by one NUL, its `args`, and `imports` its host import
 * tables (cint_ctx_config), so that the program's service calls resolve, the grants cover them
 * and `main(in U8[_] args)` reads its arguments. */
static int program_run_args(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc, char **argv,
                            const cint_host_desc *imports, const cint_state_table *state, bool width, uint32_t flags)
{
    const char *dests[3] = {NULL, NULL, NULL};
    uint8_t *grants = NULL, *args = NULL;
    size_t gbytes = 0u, abytes = 0u, at = 0u;
    int i, nd = 0, status, tail = argc;   /* tail: the first argument after "--" */
    for (i = 1; i < argc && i < tail; i++) {
        if (strcmp(argv[i], "--grant") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') return CINT_EXIT_USAGE;   /* no line, or an empty one */
            gbytes += strlen(argv[++i]) + 1u;
        } else if (strcmp(argv[i], "--arg") == 0) {
            if (i + 1 >= argc) return CINT_EXIT_USAGE;   /* no argument after it */
            abytes += strlen(argv[++i]) + 1u;
        } else if (strcmp(argv[i], "--") == 0) {
            tail = i + 1;
        } else if (nd == (state != NULL ? 3 : 2)) {
            return CINT_EXIT_USAGE;
        } else {
            dests[nd++] = strcmp(argv[i], "-") == 0 ? NULL : argv[i];
        }
    }
    for (i = tail; i < argc; i++) abytes += strlen(argv[i]) + 1u;
    if (abytes > 0u && (flags & CINT_PROGRAM_ARGS) == 0u) {
        err_str("usage: the program's main takes no arguments (SPEC-04 LS-221)\n");
        (void)fflush(stderr);
        return CINT_EXIT_USAGE;
    }
    if (gbytes > 0u) {
        if ((grants = (uint8_t *)malloc(gbytes)) == NULL) return CINT_EXIT_ENVIRONMENT;
        for (i = 1; i < tail; i++) {
            if (strcmp(argv[i], "--grant") != 0) { i += strcmp(argv[i], "--arg") == 0; continue; }
            i++;
            if (at > 0u) grants[at - 1u] = (uint8_t)'\n';
            memcpy(grants + at, argv[i], strlen(argv[i]) + 1u);
            at += strlen(argv[i]) + 1u;
        }
    }
    if (abytes > 0u) {
        if ((args = (uint8_t *)malloc(abytes)) == NULL) { free(grants); return CINT_EXIT_ENVIRONMENT; }
        at = 0u;
        for (i = 1; i < argc; i++) {
            if (i < tail && strcmp(argv[i], "--grant") == 0) { i++; continue; }
            if (i < tail && strcmp(argv[i], "--arg") == 0) i++;
            else if (i < tail) continue;
            memcpy(args + at, argv[i], strlen(argv[i]) + 1u);
            at += strlen(argv[i]) + 1u;
        }
    }
    status = program_run(program, main_fn, fuel, dests[0], dests[1], dests[2], state, width, imports, grants,
                         gbytes > 0u ? gbytes - 1u : 0u, args, abytes);   /* the LF-joined lines without the final NUL */
    free(grants);
    free(args);
    return status;
}

int cint_program_run_args(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc, char **argv,
                          const cint_host_desc *imports, const cint_state_table *state)
{
    return program_run_args(program, main_fn, fuel, argc, argv, imports, state, false, 0u);
}

int cint_program_run_argv(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc, char **argv,
                          const cint_host_desc *imports, const cint_state_table *state, uint32_t flags)
{
    return program_run_args(program, main_fn, fuel, argc, argv, imports, state, false, flags);
}

int cint_program_run_width_args(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc,
                                char **argv, const cint_host_desc *imports, const cint_state_table *state)
{
    return program_run_args(program, main_fn, fuel, argc, argv, imports, state, true, 0u);
}

int cint_program_run_width_argv(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc,
                                char **argv, const cint_host_desc *imports, const cint_state_table *state,
                                uint32_t flags)
{
    return program_run_args(program, main_fn, fuel, argc, argv, imports, state, true, flags);
}
