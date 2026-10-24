/* wb_cmd.c: the command lines of the workbench's executables (box 14 ruling BX14-03 A).
 *
 * Each spelling of SPEC-06 3.1 is a small executable on this library: cint-workbench and its
 * alias cint-repl, cint-explain, and cint-debug. Box 13's route (BX13-15) reaches them from
 * `cint`; until it lands they run under their own names. Each prints its usage with --help.
 * `cint explain` (unit 1b), the session (unit 2a), and the debugger (unit 3b-D, wb_debug.c) run;
 * a tool error is a CINT-TOOL-1 line under --json (SPEC-06 3.2, 15).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "sir.h"
#include "wb.h"
#include "wb_array.h"
#include "wb_debug.h"

#if defined(_WIN32)
#define SEP "\\"
#define EXE ".exe"
#else
#define SEP "/"
#define EXE ""
#endif

typedef struct spelling {
    const char *name;    /* as reached through `cint`: "cint workbench" */
    const char *usage;
    const char *waits;   /* what the command needs before it runs */
} spelling;

static const spelling WORKBENCH = {
    "cint workbench",
    "usage: cint workbench [--json] [--root DIR] [--cint PATH] [--out DIR] [--echo] [FILE.ci | DIR]\n"
    "       cint repl [the same options]\n"
    "Opens the interactive session on cpu-sir-interp, reading entries from\n"
    "stdin; :help lists the commands. FILE.ci is loaded as :load loads it, and DIR, the file's\n"
    "directory, or --root DIR is the root that :load reads modules from. Each entry is compiled\n"
    "by `cint emit-c` (the `cint` beside it, or --cint PATH) under --out DIR, by default\n"
    "build/workbench under the root. --echo writes each entry after its prompt, for transcripts.\n"
    "Under --json, an error that keeps the session from starting is a CINT-TOOL-1 line.\n",
    NULL};

static const spelling REPL = {
    "cint repl", NULL, NULL};

static const spelling EXPLAIN = {
    "cint explain",
    "usage: cint explain [--json] [--root DIR] [--stdlib DIR] [--cint PATH] [--out DIR | --sir DIR] FILE.ci[:NAME]\n"
    "Shows what the compiler knows about each function of a module, or about function NAME, from\n"
    "the SIR text cintc writes. It runs `cint emit-c` (the `cint` beside it, or\n"
    "--cint PATH) into --out DIR, by default build/explain under the root, or reads the SIR\n"
    "text of a program that `cint emit-c` already wrote with --sir DIR. FILE is the module's path\n"
    "under --root DIR, or a file whose directory is the root. A module that is not under the root,\n"
    "such as `fs`, is read from the standard library root, --stdlib DIR or by default `lib`\n"
    "beside that `cint`, as `cint` reads it.\n",
    NULL};

static const spelling DEBUG = {
    "cint debug",
    "usage: cint debug [--json] [--out DIR] [--echo] RECORDING | CASE\n"
    "       cint debug diff [--json] CASE [--ref BACKEND] [--cand BACKEND] [--receipt PATH]\n"
    "                       [--cint PATH] [--out DIR] [--compiler-identity HEX] [--interpreter-identity HEX]\n"
    "                       [--workbench-identity HEX]\n"
    "       cint debug replay [--json] RECORDING [--to BOUNDARY] [--receipt PATH] [--source-identity HEX]\n"
    "       cint debug locate [--json] PROGRAM.ci | RECORDING | CASE [--entry NAME [--args TYPED...]]\n"
    "                         [--depth N] [--fuel N] [--when PREDICATE | --changed BINDING] [--source-identity HEX]\n"
    "                         [--save-case PATH] [--receipt PATH] [--root DIR] [--cint PATH] [--out DIR | --sir DIR]\n"
    "Opens a recording or case, compares backends, replays, or locates a fault.\n"
    "RECORDING alone opens it read-only in a session in --out DIR, by default build/debug beside it.\n"
    "diff runs CASE from its initial state on --ref, by default cpu-c17, and --cand, by default\n"
    "cpu-sir-interp, compares the two at each boundary, runs the candidate again to the first\n"
    "difference, and localizes it binding by binding. cpu-c17 is a library that `cint build --lib`\n"
    "makes from the case's sources in --out DIR. --receipt writes the diff receipt, which --json\n"
    "prints. Exit 0 when the two agree, 3 for any other outcome. The --*-identity options give the\n"
    "source identities that a receipt names, as cint-interp's --source-identity does.\n"
    "replay replays a recording on cpu-sir-interp, compares each boundary with it, and lists the\n"
    "boundaries; --to stops after one, and --receipt writes the replay receipt, which --json prints.\n"
    "locate runs PROGRAM.ci on cpu-sir-interp under a recording, built by `cint emit-c` as\n"
    "`cint explain` builds it or read from --sir DIR, or takes a recording, and replays to the first\n"
    "boundary that ends in a fault or an error result: the report, the boundary before it, and the\n"
    "last 20 counted operations. TYPED is `I64 0` or `Bool true`. --changed BINDING locates instead\n"
    "the last boundary that changed global BINDING, by the backward scan. --save-case also writes\n"
    "the run as a case (CINT-CASE-1), with each module's source. Exit 0 when it found one, 3 when\n"
    "there is none or replay diverges.\n",
    NULL};

static void tool_error(const spelling *s, int json, int status, const char *message)
{
    fflush(stdout);
    if (json) {
        wb_buf b = {0};
        wb_f(&b, "{\"category\":\"%s\",\"exit_status\":%d,\"message\":",
             status == CINT_EXIT_USAGE ? "usage" : status == CINT_EXIT_ENVIRONMENT ? "environment" : "internal",
             status);
        wb_jstr(&b, message, strlen(message));
        wb_s(&b, ",\"schema\":\"CINT-TOOL-1\"}\n");
        if (!b.failed) fputs(b.p, stderr);
        wb_free(&b);
    } else {
        fprintf(stderr, "%s: error: %s\n", s->name, message);
    }
    fflush(stderr);
}

static int explain(int json, int argc, char **argv);
static int session(const spelling *s, int json, int argc, char **argv);

/* --help and --json may come anywhere; the first other argument names the command. */
static int run(const spelling *s, const spelling *text, int argc, char **argv)
{
    int json = 0, help = 0;
    const char *command = NULL;
    if (cli_init(&argc, &argv) != 0) {
        tool_error(s, 0, CINT_EXIT_INTERNAL, "cannot read the command line");
        return CINT_EXIT_INTERNAL;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) help = 1;
        else if (command == NULL) command = argv[i];
    }
    if (help) {
        fputs(text->usage, stdout);
        fflush(stdout);
        return CINT_EXIT_OK;
    }
    if (text == &EXPLAIN) return explain(json, argc, argv);
    if (text == &WORKBENCH) return session(s, json, argc, argv);
    if (text == &DEBUG) {
        wb_buf err = {0};
        int status = wb_debug(json, argc, argv, &err);
        if ((err.n != 0 || err.failed) &&   /* else the read-only session reported it */
            (status == CINT_EXIT_USAGE || status == CINT_EXIT_ENVIRONMENT || status == CINT_EXIT_INTERNAL)) {
            if (status == CINT_EXIT_USAGE && !json && command == NULL) fputs(text->usage, stderr);
            tool_error(s, json, status, err.n != 0 ? err.p : "out of memory");
        }
        wb_free(&err);
        return status;
    }
    tool_error(s, json, CINT_EXIT_ENVIRONMENT, text->waits);
    return CINT_EXIT_ENVIRONMENT;
}

/* ------------------------------------------------------------------ explain (unit 1b) */

/* `cint explain`: options, FILE.ci[:NAME], the SIR text, and the report on stdout. */
static int explain(int json, int argc, char **argv)
{
    const char *root = NULL, *cint = NULL, *out = NULL, *sir = NULL, *target = NULL, *name = NULL, *stdlib = NULL;
    char *file = NULL, *dir = NULL, *rel = NULL, *self = NULL, *exe = NULL, *outdir = NULL, *stage = NULL;
    char *libdir = NULL;
    int status = CINT_EXIT_INTERNAL, st = 0, given = 0;
    wb_buf err = {0}, text = {0};
    cint_sir_program *prog = NULL;
    cint_sir_error serr;
    static int64_t bytes[WB_MODULES_MAX];

    for (int i = 1; i < argc; i++) {
        const char **slot = strcmp(argv[i], "--root") == 0 ? &root : strcmp(argv[i], "--cint") == 0 ? &cint
                          : strcmp(argv[i], "--out") == 0 ? &out : strcmp(argv[i], "--sir") == 0 ? &sir
                          : strcmp(argv[i], "--stdlib") == 0 ? &stdlib : NULL;
        if (slot != NULL) {
            if (*slot != NULL || i + 1 == argc) {
                tool_error(&EXPLAIN, json, CINT_EXIT_USAGE, "an option needs one value and is given at most once");
                return CINT_EXIT_USAGE;
            }
            *slot = argv[++i];
        } else if (strcmp(argv[i], "--json") != 0) {
            if (argv[i][0] == '-' || target != NULL) {
                tool_error(&EXPLAIN, json, CINT_EXIT_USAGE, "explain takes the options of --help and one FILE.ci[:NAME]");
                return CINT_EXIT_USAGE;
            }
            target = argv[i];
        }
    }
    if (target == NULL) {
        if (!json) fputs(EXPLAIN.usage, stderr);
        tool_error(&EXPLAIN, json, CINT_EXIT_USAGE, "explain takes FILE.ci or FILE.ci:NAME");
        return CINT_EXIT_USAGE;
    }
    given = sir != NULL;
    if (out != NULL && sir != NULL) {
        tool_error(&EXPLAIN, json, CINT_EXIT_USAGE, "--out and --sir exclude each other");
        return CINT_EXIT_USAGE;
    }
    if ((file = wb_copy(target)) == NULL) goto fail;
    /* A colon after `.ci`, with a name after it, separates FILE.ci from NAME. */
    char *colon = strrchr(file, ':');
    if (colon != NULL && colon - file >= 3 && memcmp(colon - 3, ".ci", 3u) == 0 && colon[1] != '\0' &&
        strpbrk(colon + 1, "/\\") == NULL) {
        *colon = '\0';
        name = colon + 1;
    }
    size_t flen = strlen(file);
    if (flen < 4u || strcmp(file + flen - 3u, ".ci") != 0) {
        tool_error(&EXPLAIN, json, CINT_EXIT_USAGE, "explain takes FILE.ci or FILE.ci:NAME");
        status = CINT_EXIT_USAGE;
        goto done;
    }
    if (root != NULL) {   /* FILE is the module's path under the root, as for `cint run --root` */
        for (char *c = file; *c; c++) *c = *c == '\\' ? '/' : *c;
        dir = wb_copy(root);
        rel = wb_copy(file);
    } else {              /* the file's directory is the root */
        size_t cut = flen;
        while (cut > 0 && file[cut - 1u] != '/' && file[cut - 1u] != '\\') cut--;
        dir = cut > 1u ? malloc(cut) : wb_copy(cut ? SEP : ".");
        if (dir != NULL && cut > 1u) (void)snprintf(dir, cut, "%s", file);
        rel = wb_copy(file + cut);
    }
    if (dir == NULL || rel == NULL) goto fail;
    if (sir == NULL) {
        if (cint == NULL) {
            self = cli_self_dir();
            exe = self != NULL ? wb_join(self, "cint" EXE) : NULL;
            if (exe == NULL) {
                tool_error(&EXPLAIN, json, CINT_EXIT_ENVIRONMENT, "cannot locate the cint executable; give --cint PATH");
                status = CINT_EXIT_ENVIRONMENT;
                goto done;
            }
            cint = exe;
        }
        if (out == NULL && (out = outdir = wb_join(dir, "build" SEP "explain")) == NULL) goto fail;
        char *log = wb_join(out, "explain.stderr");
        int r = log != NULL ? wb_emit(cint, json, dir, rel, stdlib, out, log, &st, &err) : 1;
        size_t len = 0;
        char *said = r == 0 ? wb_read_file(log, &len) : NULL;
        if (said != NULL) {   /* cint's diagnostics, after it ends */
            (void)fwrite(said, 1, len, stderr);
            free(said);
        }
        free(log);
        if (r == 1 && err.n != 0) wb_s(&err, ", or --sir DIR for SIR text already written");
        if (r != 0) {
            status = r == 2 ? CINT_EXIT_USAGE : CINT_EXIT_ENVIRONMENT;
            tool_error(&EXPLAIN, json, status, err.p != NULL ? err.p : "out of memory");
            goto done;
        }
        if (st != CINT_EXIT_OK) {   /* cint reported it on stderr: a compile error, or why it could not build */
            status = st == CINT_EXIT_COMPILE || st == CINT_EXIT_ENVIRONMENT ? st : CINT_EXIT_INTERNAL;
            goto done;
        }
        sir = out;
    }
    if ((stage = wb_stage_of(sir)) == NULL) goto fail;
    int lst = cint_sir_load(stage, &prog, &serr);
    if (lst != CINT_SIR_OK) {
        wb_f(&err, "the SIR text under %s cannot be read: %s:%u:%u: %s", stage, serr.path, serr.line, serr.col,
             serr.text);
        status = lst == CINT_SIR_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_INTERNAL;
        tool_error(&EXPLAIN, json, status, err.p != NULL ? err.p : "out of memory");
        goto done;
    }
    if (root == NULL && given) {
        /* FILE may name the module by a longer path, as in `--sir DIR src/app/main.ci`: the
         * longest module path that ends FILE at a directory boundary is the module, and what
         * comes before it is the root (the current directory when nothing comes before it). */
        size_t best = 0;
        const char *module = NULL;
        for (uint32_t k = 0; k < prog->nmod; k++) {
            const char *mp = prog->mods[k].path;
            size_t n = strlen(mp), i = 0;
            if (n <= best || n > flen || (n < flen && file[flen - n - 1u] != '/' && file[flen - n - 1u] != '\\'))
                continue;
            size_t at = flen - n;
            while (i < n && (file[at + i] == mp[i] || (file[at + i] == '\\' && mp[i] == '/'))) i++;
            if (i == n) {
                best = n;
                module = mp;
            }
        }
        if (module != NULL) {
            size_t cut = flen - best;   /* the root and the separator after it */
            free(dir);
            free(rel);
            dir = cut > 1u ? malloc(cut) : wb_copy(cut ? SEP : ".");
            rel = wb_copy(module);
            if (dir == NULL || rel == NULL) goto fail;
            if (cut > 1u) (void)snprintf(dir, cut, "%s", file);
        }
    }
    if (stdlib == NULL) {   /* `lib` beside the cint that builds the program, or beside this executable */
        size_t cut = cint != NULL ? strlen(cint) : 0u;
        while (cut > 0 && cint[cut - 1u] != '/' && cint[cut - 1u] != '\\') cut--;
        if (cint != NULL && cut > 0) {   /* the directory, without its last separator unless it is the root */
            size_t n = cut > 1u ? cut - 1u : cut;
            char *d = malloc(n + 1u);
            if (d != NULL) (void)snprintf(d, n + 1u, "%.*s", (int)n, cint);
            libdir = d != NULL ? wb_join(d, "lib") : NULL;
            free(d);
        } else if (cint != NULL) {
            libdir = wb_copy("lib");
        } else {
            if (self == NULL) self = cli_self_dir();
            libdir = self != NULL ? wb_join(self, "lib") : NULL;
        }
        stdlib = libdir;
    }
    wb_source_sizes(prog, dir, stdlib, bytes);
    if (wb_explain(&text, prog, rel, name, prog->nmod <= WB_MODULES_MAX ? bytes : NULL, json, &err) != 0) {
        status = err.n ? CINT_EXIT_USAGE : CINT_EXIT_INTERNAL;
        tool_error(&EXPLAIN, json, status, err.n ? err.p : "out of memory");
        goto done;
    }
    (void)fwrite(text.p, 1, text.n, stdout);
    fflush(stdout);
    status = CINT_EXIT_OK;
    goto done;
fail:
    tool_error(&EXPLAIN, json, CINT_EXIT_INTERNAL, "out of memory");
    status = CINT_EXIT_INTERNAL;
done:
    cint_sir_free(prog);
    free(file);
    free(dir);
    free(rel);
    free(self);
    free(exe);
    free(outdir);
    free(stage);
    free(libdir);
    wb_free(&err);
    wb_free(&text);
    return status;
}

/* ------------------------------------------------------------------ the session (unit 2a) */

static int session(const spelling *s, int json, int argc, char **argv)
{
    const char *root = NULL, *cint = NULL, *out = NULL, *target = NULL;
    char *dir = NULL, *load = NULL, *self = NULL, *exe = NULL, *outdir = NULL;
    wb_session_options o;
    int status = CINT_EXIT_INTERNAL, echo = 0;
    memset(&o, 0, sizeof o);
    for (int i = 1; i < argc; i++) {
        const char **slot = strcmp(argv[i], "--root") == 0 ? &root : strcmp(argv[i], "--cint") == 0 ? &cint
                          : strcmp(argv[i], "--out") == 0 ? &out : NULL;
        if (slot != NULL) {
            if (*slot != NULL || i + 1 == argc) {
                tool_error(s, json, CINT_EXIT_USAGE, "an option needs one value and is given at most once");
                return CINT_EXIT_USAGE;
            }
            *slot = argv[++i];
        } else if (strcmp(argv[i], "--echo") == 0) {
            echo = 1;
        } else if (strcmp(argv[i], "--json") != 0) {
            if (argv[i][0] == '-' || target != NULL) {
                tool_error(s, json, CINT_EXIT_USAGE, "the session takes the options of --help and one FILE.ci or DIR");
                return CINT_EXIT_USAGE;
            }
            target = argv[i];
        }
    }
    size_t tlen = target != NULL ? strlen(target) : 0;
    int file = tlen > 3u && strcmp(target + tlen - 3u, ".ci") == 0;
    if (target != NULL && !file && root != NULL) {
        tool_error(s, json, CINT_EXIT_USAGE, "--root and a root DIR exclude each other");
        return CINT_EXIT_USAGE;
    }
    if (root != NULL || target == NULL || !file) {   /* FILE, if any, is a path under the root */
        dir = wb_copy(root != NULL ? root : target != NULL ? target : ".");
        load = file ? wb_copy(target) : NULL;
        if (dir == NULL || (file && load == NULL)) goto fail;
    } else {                                         /* the file's directory is the root */
        size_t cut = tlen;
        while (cut > 0 && target[cut - 1u] != '/' && target[cut - 1u] != '\\') cut--;
        dir = cut > 1u ? malloc(cut) : wb_copy(cut ? SEP : ".");
        load = wb_copy(target + cut);
        if (dir == NULL || load == NULL) goto fail;
        if (cut > 1u) (void)snprintf(dir, cut, "%s", target);
    }
    if (cint == NULL) {
        self = cli_self_dir();
        exe = self != NULL ? wb_join(self, "cint" EXE) : NULL;
        if (exe == NULL) {
            tool_error(s, json, CINT_EXIT_ENVIRONMENT, "cannot locate the cint executable; give --cint PATH");
            status = CINT_EXIT_ENVIRONMENT;
            goto done;
        }
        cint = exe;
    }
    FILE *probe = cli_fopen(cint, "rb");
    if (probe == NULL) {   /* every entry needs it, so the session does not start without it */
        wb_buf why = {0};
        wb_f(&why, "cannot find %s; give --cint PATH", cint);
        tool_error(s, json, CINT_EXIT_ENVIRONMENT, why.p != NULL ? why.p : "out of memory");
        wb_free(&why);
        status = CINT_EXIT_ENVIRONMENT;
        goto done;
    }
    fclose(probe);
    if (out == NULL && (out = outdir = wb_join(dir, "build" SEP "workbench")) == NULL) goto fail;
    o.cint = cint;
    o.root = dir;
    o.out = out;
    o.load = load;
    o.echo = echo;
    status = wb_session(&o, stdin);
    goto done;
fail:
    tool_error(s, json, CINT_EXIT_INTERNAL, "out of memory");
    status = CINT_EXIT_INTERNAL;
done:
    free(dir);
    free(load);
    free(self);
    free(exe);
    free(outdir);
    return status;
}

int wb_main_workbench(int argc, char **argv)
{
    return run(&WORKBENCH, &WORKBENCH, argc, argv);
}

int wb_main_repl(int argc, char **argv)
{
    return run(&REPL, &WORKBENCH, argc, argv);
}

int wb_main_explain(int argc, char **argv)
{
    return run(&EXPLAIN, &EXPLAIN, argc, argv);
}

int wb_main_debug(int argc, char **argv)
{
    return run(&DEBUG, &DEBUG, argc, argv);
}
