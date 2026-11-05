/* Compatibility with small hooks this tree exposes to its diagnostics. */
#include "nv2a_dk.h"

#include <stdio.h>
#include <stdlib.h>

int xbox_video_widescreen(void)
{
    const char *e = getenv("RECOMP_WIDESCREEN");
    return e && atoi(e) != 0;
}

/* nv2a_pb_scan brackets its work with these GL profiler hooks to time a
 * method packet. The Deko3D executor keeps its own counters and prints
 * them in its [perf] report, so there is nothing to bracket here. */
void nv2a_pb_time_begin(void) {}
void nv2a_pb_time_end(unsigned words) { (void)words; }

/* Preserve the frame-number hooks used by the game-side diagnostics. */
uint32_t nv2a_gl_flips(void) { return nv2a_dk_frame_count(); }

static void unavailable(const char *what)
{
    static unsigned warned;
    if (!warned++)
        fprintf(stderr, "  [DK] %s is unavailable with the Deko3D renderer\n", what);
}

void nv2a_gl_dump_burst(int shots, int every)
{
    (void)shots;
    (void)every;
    unavailable("GL framebuffer dumps");
}

void nv2a_gl_trace_frames(int frames)
{
    (void)frames;
    unavailable("GL frame tracing");
}
