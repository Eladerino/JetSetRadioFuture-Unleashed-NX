/*
 * Device-register access from lifted code.
 *
 * recomp_types.h routes every guest load/store whose address falls in
 * [RECOMP_MMIO_LO, RECOMP_MMIO_HI) through these two functions instead of the
 * flat memory mapping. Which device answers is decided here, by address; a
 * range nobody claims behaves as the plain memory it always was, so a title
 * that pokes at a register we do not model sees the same thing it did before
 * this existed.
 *
 * The APU is the first customer. Xbox DirectSound programs the MCPX audio
 * processor through its registers -- voice lists, the frame counter mode,
 * the DSP handshake -- and reads the play cursor back through the voice
 * structures the hardware updates in RAM. Backed as plain memory none of
 * that happens: the emulated APU never sees a write, never runs a frame,
 * and the cursor never moves, which CRI's ADX streamer (JSRF's music
 * player) diagnoses after a few hundred frames as a dead drive and the
 * game turns into a "disc may be dirty or damaged" screen.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "xbox_memory_layout.h"

uint32_t (*g_recomp_mmio_read_fn)(uint32_t addr, unsigned size);
void (*g_recomp_mmio_write_fn)(uint32_t addr, uint32_t val, unsigned size);

static int s_mmio_trace = -1;

static inline volatile void *plain(uint32_t addr)
{
    return (volatile void *)((uintptr_t)addr + xbox_GetMemoryOffset());
}

uint32_t recomp_mmio_read(uint32_t addr, unsigned size)
{
    uint32_t v;
    if (s_mmio_trace < 0) s_mmio_trace = getenv("RECOMP_MMIO_TRACE") ? 1 : 0;
    if (g_recomp_mmio_read_fn)
        v = g_recomp_mmio_read_fn(addr, size);
    else switch (size) {
    case 1:  v = *(volatile uint8_t *)plain(addr); break;
    case 2:  v = *(volatile uint16_t *)plain(addr); break;
    default: v = *(volatile uint32_t *)plain(addr); break;
    }
    if (s_mmio_trace)
        fprintf(stderr, "  [MMIO] rd%u 0x%08X -> 0x%08X\n", size * 8, addr, v);
    return v;
}

void recomp_mmio_write(uint32_t addr, uint32_t val, unsigned size)
{
    if (s_mmio_trace < 0) s_mmio_trace = getenv("RECOMP_MMIO_TRACE") ? 1 : 0;
    if (s_mmio_trace)
        fprintf(stderr, "  [MMIO] wr%u 0x%08X <- 0x%08X\n", size * 8, addr, val);
    /* Keep the plain-memory image current as well, so a later read of a
     * register the model does not answer (and the debugger) see the value. */
    switch (size) {
    case 1:  *(volatile uint8_t *)plain(addr) = (uint8_t)val; break;
    case 2:  *(volatile uint16_t *)plain(addr) = (uint16_t)val; break;
    default: *(volatile uint32_t *)plain(addr) = val; break;
    }
    if (g_recomp_mmio_write_fn)
        g_recomp_mmio_write_fn(addr, val, size);
}

/* The device-register calls of recomp_types.h's fast guest memory
 * (RECOMP_FAST_MEM, AArch64 on the Switch). Lifted code calls these from
 * inline asm that tells the compiler only x0, x1, x16, x17, x30 and the
 * flags change, so the guest registers it holds in host registers survive
 * the call. Everything else the C calling convention lets a callee change
 * -- x2..x18 and the vector registers outside v8..v15 -- is saved here
 * around the C handler. x0 is the address, x1 the size (read: the value
 * comes back in x0) or the value (write, with the size in x2). */
#if defined(__SWITCH__) && defined(__aarch64__)
#define MMIO_PRESERVING(name, target)                                   \
    __asm__(".text\n"                                                   \
            ".global " #name "\n"                                       \
            ".type " #name ", %function\n"                              \
            ".p2align 2\n"                                              \
            #name ":\n"                                                 \
            "  sub  sp, sp, #560\n"                                     \
            "  stp  x29, x30, [sp, #0]\n"                               \
            "  mov  x29, sp\n"                                          \
            "  stp  x2, x3, [sp, #16]\n"                                \
            "  stp  x4, x5, [sp, #32]\n"                                \
            "  stp  x6, x7, [sp, #48]\n"                                \
            "  stp  x8, x9, [sp, #64]\n"                                \
            "  stp  x10, x11, [sp, #80]\n"                              \
            "  stp  x12, x13, [sp, #96]\n"                              \
            "  stp  x14, x15, [sp, #112]\n"                             \
            "  stp  x16, x17, [sp, #128]\n"                             \
            "  str  x18, [sp, #144]\n"                                  \
            "  stp  q0, q1, [sp, #160]\n"                               \
            "  stp  q2, q3, [sp, #192]\n"                               \
            "  stp  q4, q5, [sp, #224]\n"                               \
            "  stp  q6, q7, [sp, #256]\n"                               \
            "  stp  q16, q17, [sp, #288]\n"                             \
            "  stp  q18, q19, [sp, #320]\n"                             \
            "  stp  q20, q21, [sp, #352]\n"                             \
            "  stp  q22, q23, [sp, #384]\n"                             \
            "  stp  q24, q25, [sp, #416]\n"                             \
            "  stp  q26, q27, [sp, #448]\n"                             \
            "  stp  q28, q29, [sp, #480]\n"                             \
            "  stp  q30, q31, [sp, #512]\n"                             \
            "  bl   " #target "\n"                                      \
            "  ldp  q30, q31, [sp, #512]\n"                             \
            "  ldp  q28, q29, [sp, #480]\n"                             \
            "  ldp  q26, q27, [sp, #448]\n"                             \
            "  ldp  q24, q25, [sp, #416]\n"                             \
            "  ldp  q22, q23, [sp, #384]\n"                             \
            "  ldp  q20, q21, [sp, #352]\n"                             \
            "  ldp  q18, q19, [sp, #320]\n"                             \
            "  ldp  q16, q17, [sp, #288]\n"                             \
            "  ldp  q6, q7, [sp, #256]\n"                               \
            "  ldp  q4, q5, [sp, #224]\n"                               \
            "  ldp  q2, q3, [sp, #192]\n"                               \
            "  ldp  q0, q1, [sp, #160]\n"                               \
            "  ldr  x18, [sp, #144]\n"                                  \
            "  ldp  x16, x17, [sp, #128]\n"                             \
            "  ldp  x14, x15, [sp, #112]\n"                             \
            "  ldp  x12, x13, [sp, #96]\n"                              \
            "  ldp  x10, x11, [sp, #80]\n"                              \
            "  ldp  x8, x9, [sp, #64]\n"                                \
            "  ldp  x6, x7, [sp, #48]\n"                                \
            "  ldp  x4, x5, [sp, #32]\n"                                \
            "  ldp  x2, x3, [sp, #16]\n"                                \
            "  ldp  x29, x30, [sp, #0]\n"                               \
            "  add  sp, sp, #560\n"                                     \
            "  ret\n"                                                   \
            ".size " #name ", .-" #name "\n")
MMIO_PRESERVING(recomp_mmio_read_preserving, recomp_mmio_read);
MMIO_PRESERVING(recomp_mmio_write_preserving, recomp_mmio_write);
#endif
