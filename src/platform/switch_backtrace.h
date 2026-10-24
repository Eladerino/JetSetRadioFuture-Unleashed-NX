/*
 * switch_backtrace.h -- <execinfo.h>'s backtrace() for Nintendo Switch.
 *
 * Horizon has no execinfo.h and no dladdr (a statically linked .nro has
 * no dynamic loader to ask). The addresses are the useful part anyway:
 * they resolve offline against the linker map the port's CMake keeps,
 * which is the model the Windows build already uses for crash reports.
 * So: walk with the same unwinder android_backtrace.h uses, and print
 * raw addresses, no names.
 */
#ifndef SWITCH_BACKTRACE_H
#define SWITCH_BACKTRACE_H

#include <unwind.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

struct jsrf_bt_state { void **buf; int n, max; };

static _Unwind_Reason_Code jsrf_bt_step(struct _Unwind_Context *ctx, void *arg)
{
    struct jsrf_bt_state *st = (struct jsrf_bt_state *)arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc) {
        if (st->n >= st->max) return _URC_END_OF_STACK;
        st->buf[st->n++] = (void *)pc;
    }
    return _URC_NO_REASON;
}

static inline int backtrace(void **buf, int max)
{
    struct jsrf_bt_state st;
    st.buf = buf; st.n = 0; st.max = max;
    _Unwind_Backtrace(jsrf_bt_step, &st);
    return st.n;
}

static inline void backtrace_symbols_fd(void *const *buf, int n, int fd)
{
    for (int i = 0; i < n; i++) {
        char line[32];
        int len = snprintf(line, sizeof line, "%p\n", buf[i]);
        if (len > 0) { ssize_t ignored = write(fd, line, (size_t)len); (void)ignored; }
    }
}

#endif /* SWITCH_BACKTRACE_H */
