/* ptx_interp.h: a PTX interpreter for the stand-in driver (rt/tests/cuda_fake.c built with
 * CINT_FAKE_PTX; box 11 unit 4). It runs the subset compiler/back_ptx.ci writes, so a
 * gpu-cuda build can run its kernels on every leg with no GPU. */
#ifndef PTX_INTERP_H
#define PTX_INTERP_H

#include <stddef.h>
#include <stdint.h>

typedef struct ptx_module ptx_module;
typedef struct ptx_entry ptx_entry;

/* The host address of `bytes` bytes at device address `addr`, or NULL when they lie outside
 * every allocation and mapping of the driver. */
typedef void *(*ptx_translate)(uint64_t addr, size_t bytes);

/* Parses NUL-terminated PTX text; NULL with a message in log (cap bytes) when a line is
 * outside the subset. */
ptx_module *ptx_load(const char *text, char *log, size_t cap);
void ptx_free(ptx_module *m);
ptx_entry *ptx_find(ptx_module *m, const char *name);
/* Runs blocks [0, grid) of `block` work-items of entry e, the parameters as cuLaunchKernel
 * takes them; within the launch the items run one at a time, in descending order unless
 * `ascending`. 0, 700 (CUDA_ERROR_ILLEGAL_ADDRESS) for an address translate refuses, or
 * 702 (CUDA_ERROR_LAUNCH_TIMEOUT) when an item runs past the step limit. */
int ptx_launch(const ptx_entry *e, unsigned grid, unsigned block, void **params, ptx_translate translate,
               int ascending);

#endif
