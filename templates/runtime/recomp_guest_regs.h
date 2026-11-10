#pragma once

/* FIX2: the guest integer register file as global register variables.
 *
 * Force-included ahead of recomp_types.h into the translation units
 * that run guest code, only when the build sets JSRF_GUEST_REGS on
 * AArch64 GCC. The eight pinned names then have no storage anywhere:
 * recomp_types.h suppresses their TLS declarations (it keys on
 * JSRF_GUEST_REGS_IN_CPU, defined below), xbox_memory_layout.c
 * suppresses their .tbss definitions, and any TU that missed the
 * force-include fails at link rather than silently splitting the
 * register file in two. Outside the guard this header is empty and
 * the names fall back to the existing TLS declarations, so the
 * shipped model stays one flag away. */

#if defined(__aarch64__) && defined(__GNUC__) && !defined(__clang__) \
        && !defined(__cplusplus) && defined(JSRF_GUEST_REGS)

#define JSRF_GUEST_REGS_IN_CPU 1

#include <stdint.h>

register uint32_t g_eax __asm__("w19");
register uint32_t g_ecx __asm__("w20");
register uint32_t g_edx __asm__("w21");
register uint32_t g_ebx __asm__("w22");
register uint32_t g_esi __asm__("w23");
register uint32_t g_edi __asm__("w24");
register uint32_t g_esp __asm__("w25");
register int g_fp_top __asm__("w27");

/* The call seam (src/kernel/recomp_guest_regs.c): runs fn(ctx) with
 * the pinned registers saved, zeroed on entry, and restored on
 * return, so no pinned register is ever live across a call into
 * unconverted code. */
typedef void *(*jsrf_guest_regs_fn)(void *ctx);
void *jsrf_guest_regs_call(jsrf_guest_regs_fn fn, void *ctx);

#endif
