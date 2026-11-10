/* FIX2: the call seam for the pinned guest registers.
 *
 * jsrf_guest_regs_call(fn, ctx) runs fn on the caller's stack in the
 * one translation unit that owns the register declarations, so no
 * pinned register is ever live across a call into unconverted code:
 * the trampoline saves the caller's x19-x27, zeroes w19-w25 and w27
 * (a recycled host thread must start the guest with zeroed registers
 * exactly as fresh TLS .tbss used to provide), calls fn(ctx), and
 * restores the caller's registers on return.
 *
 * Guest threads keep their x87/SSE state in a per-thread
 * GuestThreadState; the register file itself has no storage to save
 * -- it IS the registers. */

#include <stdint.h>

typedef struct GuestThreadState {
    double   fp_stack[8];
    int      fp_top;
    uint32_t reserved[7];
} GuestThreadState;

static __thread GuestThreadState s_guest_thread_state;

GuestThreadState *jsrf_guest_thread_state(void)
{
    return &s_guest_thread_state;
}

#if defined(__aarch64__)
__asm__(
    "\t.text\n"
    "\t.globl jsrf_guest_regs_call\n"
    "\t.type jsrf_guest_regs_call, %function\n"
    "jsrf_guest_regs_call:\n"
    "\tstp x29, x30, [sp, #-16]!\n"
    "\tmov x29, sp\n"
    "\tstp x19, x20, [sp, #-16]!\n"
    "\tstp x21, x22, [sp, #-16]!\n"
    "\tstp x23, x24, [sp, #-16]!\n"
    "\tstp x25, x26, [sp, #-16]!\n"
    "\tstp x27, x28, [sp, #-16]!\n"
    "\tmov x16, x0\n"
    "\tmov x0, x1\n"
    "\tmov w19, wzr\n"
    "\tmov w20, wzr\n"
    "\tmov w21, wzr\n"
    "\tmov w22, wzr\n"
    "\tmov w23, wzr\n"
    "\tmov w24, wzr\n"
    "\tmov w25, wzr\n"
    "\tmov w27, wzr\n"
    "\tblr x16\n"
    "\tldp x27, x28, [sp], #16\n"
    "\tldp x25, x26, [sp], #16\n"
    "\tldp x23, x24, [sp], #16\n"
    "\tldp x21, x22, [sp], #16\n"
    "\tldp x19, x20, [sp], #16\n"
    "\tldp x29, x30, [sp], #16\n"
    "\tret\n"
    "\t.size jsrf_guest_regs_call, .-jsrf_guest_regs_call\n"
);
#endif
