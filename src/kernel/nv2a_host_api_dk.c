/*
 * The renderer's host-facing API for the Deko3D build.
 *
 * Code outside the renderer calls a handful of renderer functions by name:
 * port/main.c hands the renderer the collected shader seed,
 * nv2a_pb_exec_dk.c asks whether logging is quiet, and
 * port/recomp_manual.c drives its widescreen work off the renderer's
 * wide state. In the OpenGL build src/nv2a_gl/nv2a_gl.c
 * answers all of these; the Deko3D build does not link that file, so this
 * one answers them here, with the same option semantics on Switch. It is
 * compiled only into Deko3D builds (src/kernel/CMakeLists.txt), so the
 * OpenGL build's own definitions are never duplicated.
 */
#include <stdlib.h>
#include <string.h>

/* ---- the shader seed (port/main.c) -------------------------------------
 * The seed is a list of shaders collected by playing the game, which the
 * OpenGL renderer uses to build its shader cache ahead of time. The
 * Deko3D renderer builds its pack from the game's own XBE instead
 * (dk_compile.c) and has no seed import, so the bytes are kept, unused,
 * for parity with the OpenGL build's hand-off. */
static const void *s_seed_data;
static size_t      s_seed_len;

void nv2a_set_shader_seed(const void *data, size_t len)
{
    s_seed_data = data;
    s_seed_len = len;
    (void)s_seed_data;
    (void)s_seed_len;
}

/* ---- quiet logging (src/kernel/nv2a_pb_exec_dk.c) -----------------------
 * RECOMP_QUIET=1 silences the executor's unhandled-method notes and its
 * report. Read once; options.txt lines reach this code as environment
 * variables, as everywhere else in the port. */
int xbox_log_quiet(void)
{
    static int quiet = -1;
    if (quiet < 0) {
        const char *v = getenv("RECOMP_QUIET");
        quiet = (v && atoi(v) != 0) ? 1 : 0;
    }
    return quiet;
}

/* ---- widescreen state (port/recomp_manual.c) ----------------------------
 * The same state and readings as src/nv2a_gl/nv2a_gl.c, minus the window
 * and menu parts, which the Deko3D build does not have. On Switch the
 * loader turns options.txt's widescreen= line into RECOMP_WIDESCREEN
 * (port/switch/jsrf_switch.c), which is what decides here. */
static int g_wide = -1;

static float g_wide_aspect = 16.0f / 9.0f, g_wide_s = 0.75f;

void nv2a_set_wide_aspect(float a)
{
    if (!(a >= 16.0f / 9.0f)) a = 16.0f / 9.0f;     /* NaN too */
    if (a > 2.4f) a = 2.4f;
    if (a == g_wide_aspect) return;
    g_wide_s = (a == 16.0f / 9.0f) ? 0.75f : 4.0f / (3.0f * a);
    g_wide_aspect = a;
}

float nv2a_wide_aspect(void) { return g_wide_aspect; }
float nv2a_wide_squeeze(void) { return g_wide_s; }

int nv2a_widescreen(void)
{
    if (g_wide < 0) {
        const char *v = getenv("RECOMP_WIDESCREEN");
        if (v && *v) g_wide = atoi(v) != 0;
        else g_wide = 0;
        /* Merge migration (2026-11-15): the stretch look is part of
         * widescreen now, so a JSRF_WIDE_STRETCH=1 line left in an
         * options file from before the merge reads as widescreen on.
         * The Switch loader does the same to the environment itself;
         * this covers any start path that reaches here first. */
        if (!g_wide && (v = getenv("JSRF_WIDE_STRETCH")) != NULL && atoi(v) != 0)
            g_wide = 1;
        if ((v = getenv("RECOMP_WIDE_ASPECT")) != NULL && atof(v) > 0.0)
            nv2a_set_wide_aspect((float)atof(v));
    }
    return g_wide;
}

void nv2a_set_widescreen(int on) { g_wide = on ? 1 : 0; }

/* A title can keep a screen 4:3 (JSRF keeps its title and boot logos that
 * way); such a screen is not one the game side widened. Starts at 1: a
 * title that never says anything is widened throughout. */
static volatile int g_wide_scene = 1;

void nv2a_set_wide_scene(int on) { g_wide_scene = on ? 1 : 0; }

/* The stretch look merged into widescreen (2026-11-15): there is one
 * display mode now -- widescreen on presents the untouched 4:3 picture
 * filling the screen, and the guest-side widening never runs. This
 * function used to latch JSRF_WIDE_STRETCH from the environment once,
 * with no setter anywhere, so after a boot with the line set, the
 * guest side disagreed with the presenter for the whole session
 * (issue #4's "didn't show as off"). Its answer now follows the
 * merged widescreen state live, through the same state the menu's
 * row drives, which makes nv2a_wide_scene() false under every option
 * combination by construction. JSRF_WIDE_STRETCH itself survives
 * only as the migration input read in nv2a_widescreen() above. */
static int nv2a_wide_stretch(void)
{
    return nv2a_widescreen();
}

int nv2a_wide_scene(void)
{
    return !nv2a_wide_stretch() && nv2a_widescreen() && g_wide_scene;
}
