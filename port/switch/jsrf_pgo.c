/* GEN PGO: runtime support for the profile-generate build.
 *
 * jsrf_pgo_start(base) runs right after the log opens: it makes
 * <base>/pgo, points gcov at it (GCOV_PREFIX, with
 * GCOV_PREFIX_STRIP baked at build time from the build directory's
 * depth, so the .gcda files land in one tree instead of mirroring
 * the build machine's whole path), announces the build in the log,
 * and starts the flush thread that dumps counters every 60 s -- a
 * HOME-close never runs exit code, so the periodic dump is what
 * guarantees a profile survives the session. Each flush is a dump
 * plus a reset (see jsrf_pgo_flush_once for why the reset is
 * load-bearing) and logs a heartbeat line per tick. A bonus dump
 * hangs off the normal exit path where the platform glue provides
 * one. The flush is serialized on a spin flag: __gcov_dump is not
 * reentrant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

extern void __gcov_dump(void);
extern void __gcov_reset(void);

#ifndef JSRF_PGO_GCOV_STRIP
/* Fallback for a compile without the CMake-baked value: the strip
 * for the standard jsrf-switch-build-<name>/build-switch layout. */
#define JSRF_PGO_GCOV_STRIP 3
#endif

static volatile int s_pgo_flushing;
static Thread s_pgo_thread;
static char s_pgo_stack[64 * 1024] __attribute__((aligned(0x1000)));

/* One flush: dump the counters gathered since the last flush, then
 * reset.  The reset is load-bearing, not hygiene: this libgcov
 * latches every gcov root "dumped" inside __gcov_dump
 * (__gcov_dump_one skips a root whose flags byte has bit 0 set, and
 * sets it after dumping), so a dump-only loop writes exactly one
 * profile per boot -- proven on hardware 2026-11-11, when boot 3's
 * clean 3-cycle patrol never reached its .gcda (files frozen at the
 * first tick, runs=1).  __gcov_reset clears the latch along with
 * the counters, and each dump merges into the .gcda already on the
 * card, so the file still ends up holding the whole session's
 * counts.  This libgcov ships no __gcov_flush, so the pair is
 * spelled out.  Returns 1 when a flush ran. */
static int jsrf_pgo_flush_once(void)
{
    if (__atomic_exchange_n(&s_pgo_flushing, 1, __ATOMIC_ACQ_REL))
        return 0;
    __gcov_dump();
    __gcov_reset();
    __atomic_store_n(&s_pgo_flushing, 0, __ATOMIC_RELEASE);
    return 1;
}

void jsrf_pgo_exit_dump(void)
{
    jsrf_pgo_flush_once();
}

static void jsrf_pgo_flush_thread(void *arg)
{
    unsigned tick = 0;

    (void)arg;
    for (;;) {
        svcSleepThread(60000000000ULL);   /* 60 s */
        tick++;
        /* The heartbeat is the recurrence proof: one line per tick,
         * printed only after the flush returns, so a boot log alone
         * separates "flushes recurred" from "the first flush never
         * returned" and from "later flushes were skipped". */
        if (jsrf_pgo_flush_once())
            fprintf(stderr, "[pgo] tick %u dumped\n", tick);
        else
            fprintf(stderr, "[pgo] tick %u skipped\n", tick);
        fflush(stderr);
    }
}

void jsrf_pgo_start(const char *base)
{
    char dir[512];
    char strip[8];
    Result rc;

    snprintf(dir, sizeof dir, "%s/pgo", base);
    mkdir(dir, 0777);
    setenv("GCOV_PREFIX", dir, 1);
    snprintf(strip, sizeof strip, "%d", JSRF_PGO_GCOV_STRIP);
    setenv("GCOV_PREFIX_STRIP", strip, 1);
    fprintf(stderr, "[pgo] [build] pgo=instrumented (gen TUs), gcov prefix %s, strip %d\n",
            dir, JSRF_PGO_GCOV_STRIP);
    fflush(stderr);

    rc = threadCreate(&s_pgo_thread, jsrf_pgo_flush_thread, NULL,
                      s_pgo_stack, sizeof s_pgo_stack, 0x2C, -2);
    if (R_SUCCEEDED(rc))
        threadStart(&s_pgo_thread);
}
