/* diag.h: diagnostics of cint-seed (SPEC-09 SEED-07, DIAG-01).
 *
 * The seed reports the first error and stops: one line
 * `<path>:<line>:<col>: error C<code>: <text>`, where <path> is
 * module-relative, followed by the source line and a caret line (tabs kept,
 * SPEC-04 LS-279), on standard error, with exit status 2. Codes are those of
 * the SPEC-04 17.2 table (LS-313, slice 2 patch D-4).
 */
#ifndef SEED_DIAG_H
#define SEED_DIAG_H

#include <stdint.h>

#define C_SYNTAX "C1050"     /* expected token or construct */
#define C_BOOT "C9100"       /* outside cint-boot-1 (SEED-06) */
#define C_LIMIT "C9001"      /* a table of the seed is full (SEED-11) */
#define C_NEST "C9004"       /* nesting beyond the seed's limit (SEED-15) */
#define C_UNSUP "C9102"      /* inside cint-boot-1, not supported by the seed; the text names the limit */

_Noreturn void diag_at(int32_t module, uint32_t line, uint32_t col, const char *code, const char *fmt, ...);
_Noreturn void diag_tok(int32_t module, uint32_t tok, const char *code, const char *fmt, ...);
_Noreturn void diag_fatal(const char *fmt, ...);
/* Formats into a static buffer (one use at a time). */
const char *fmtbuf(const char *fmt, ...);

#endif /* SEED_DIAG_H */
