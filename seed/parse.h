/* parse.h: the parser of cint-seed (SPEC-04 section 18 restricted to the
 * cint-boot-1 productions of SPEC-09 5.5, BOOT-01).
 *
 * The parser does not recurse (SEED-11): expressions are parsed by operator
 * precedence with an explicit frame stack into postfix node order, and
 * statements by a state machine over an explicit construct stack. Both stacks
 * are sized from the module's token count.
 */
#ifndef SEED_PARSE_H
#define SEED_PARSE_H

#include "tables.h"

/* Binary operator classes. */
bool op_is_arith(int op);     /* * / % *% + - +% -% and the saturating forms */
bool op_is_bitwise(int op);   /* & ^ | */
bool op_is_shift(int op);     /* << >> <<% */
bool op_is_cmp(int op);       /* == != < <= > >= */
/* The binary operator of a compound assignment (OP_ADDA -> OP_ADD), or 0. */
int op_of_assign(int op);
/* A reserved type-name spelling [IUQT][0-9]+ (SPEC-04 LS-17). */
bool tok_reserved(const seed_module *m, uint32_t t);

#endif /* SEED_PARSE_H */
