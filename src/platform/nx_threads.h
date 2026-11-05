/*
 * Small Switch thread helpers for code shared with the render back ends
 * (src/kernel/nv2a_backend_queue.c): which core the calling thread is on,
 * yielding, setting priority, and moving a worker beside the thread it
 * serves. Header-only; the Switch implementations use libnx directly, in
 * the same terms as port/switch/jsrf_switch.c's own thread helpers.
 */
#ifndef PLATFORM_NX_THREADS_H
#define PLATFORM_NX_THREADS_H

#if defined(__SWITCH__)

#include <switch.h>

/* The core the calling thread is running on right now. */
static inline int nx_thread_core(void)
{
    return (int)svcGetCurrentProcessorNumber();
}

/* Give up the rest of this thread's slice. */
static inline void nx_thread_yield(void)
{
    svcSleepThread(0);
}

/* This tree keeps no registry of worker threads (the profiler in
 * port/switch/jsrf_switch.c tracks only the threads it places itself),
 * so there is nothing to register with. Kept for the caller's shape. */
static inline void nx_thread_track(void *fn)
{
    (void)fn;
}

/* prio is a Horizon thread priority (0..63; lower is more urgent). */
static inline void nx_thread_set_priority(int prio)
{
    if (prio < 0) prio = 0;
    if (prio > 63) prio = 63;
    svcSetThreadPriority(CUR_THREAD_HANDLE, (u32)prio);
}

/* Pin the calling thread to an allowed core other than avoid_core (the
 * core of the thread it serves), when the title may use one (cores 0..2;
 * core 3 is the system's). Returns the core the thread ends up on; when
 * no other core is allowed it stays where it is. */
static inline int nx_thread_place_beside(int avoid_core)
{
    u64 m = 0;
    u32 mask = 1;
    int core;
    if (R_SUCCEEDED(svcGetInfo(&m, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
        mask = (u32)m & 0x7;
    if (!mask) mask = 1;
    for (core = 0; core < 3; core++) {
        if (core == avoid_core) continue;
        if (!(mask & (1u << core))) continue;
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
        break;
    }
    return nx_thread_core();
}

#else /* !__SWITCH__: plain POSIX fallbacks, same shapes. */

#include <sched.h>

static inline int nx_thread_core(void)        { return sched_getcpu(); }
static inline void nx_thread_yield(void)      { sched_yield(); }
static inline void nx_thread_track(void *fn)  { (void)fn; }
static inline void nx_thread_set_priority(int prio) { (void)prio; }
static inline int nx_thread_place_beside(int avoid_core)
{
    (void)avoid_core;
    return sched_getcpu();
}

#endif
#endif /* PLATFORM_NX_THREADS_H */
