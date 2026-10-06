/* scan.h: source checks and the scanner of cint-seed (SPEC-04 section 3,
 * SPEC-09 5.5 rows IDENT to BLOCK_COMMENT). */
#ifndef SEED_SCAN_H
#define SEED_SCAN_H

#include "tables.h"

/* Keyword spelling of KW_* and operator spelling of OP_*, for messages. */
const char *kw_text(int kw);
const char *op_text(int op);

#endif /* SEED_SCAN_H */
