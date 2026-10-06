/* cint_bridge_internal.h: the host primitives that rt/cint_bridge.c lends to
 * rt/cint_build.c (the output set and the build driver; rt/OPEN.md RT-OQ-29).
 * Not part of cint-rt-3: only the two bridge files include it.
 *
 * Paths are UTF-8. On Windows they are converted to UTF-16, and a path with
 * the \\?\ prefix has its '/' separators converted to '\'.
 */
#ifndef CINT_BRIDGE_INTERNAL_H
#define CINT_BRIDGE_INTERNAL_H

#include "cint_bridge.h"

/* A path cint_commit_output accepts: non-empty UTF-8, not ending in a separator. */
bool cint_bridge_path_ok(const char *path);
/* A CINTC-12 module-relative path (see cint_bridge_read_input). */
bool cint_bridge_rel_ok(const char *rel);
/* '/', and on Windows also '\'. */
bool cint_bridge_is_sep(char c);
/* a, sep, b concatenated in a new buffer, or NULL. */
char *cint_bridge_join(const char *a, const char *sep, const char *b);
/* "<path>.stage-<k>" in a new buffer, or NULL. */
char *cint_bridge_stage_name(const char *path, unsigned k);
/* Lowercase hex of the SHA-256 of p[0..n). */
void cint_bridge_sha256_hex(const uint8_t *p, size_t n, char hex[65]);
/* Creates `path` exclusively, writes and flushes it: 1 written, 0 the name
 * exists, -1 a failure (the file is then removed). */
int cint_bridge_write_new(const char *path, const uint8_t *p, size_t len);
/* 1 made, 0 exists, -1 error. */
int cint_bridge_make_dir(const char *path);
/* Removes a file (dir false) or an empty directory (dir true); best effort. */
void cint_bridge_remove(const char *path, bool dir);
/* Reads a regular file of at most `limit` bytes, not through a link at its last
 * segment, into a new buffer of len + 1 bytes (free). It records no input
 * identity. False on any failure. */
bool cint_bridge_read_file(const char *path, size_t limit, uint8_t **bytes, size_t *len);
/* The compiler's module descriptor (compiler/main.ci, or the stub). */
extern const cint_module_info cm_4_main;
/* CINT_BRIDGE_STAGE_TRIES, or cint_bridge_test_stage_tries when a test build sets it. */
unsigned cint_bridge_stage_tries(void);

#endif /* CINT_BRIDGE_INTERNAL_H */
