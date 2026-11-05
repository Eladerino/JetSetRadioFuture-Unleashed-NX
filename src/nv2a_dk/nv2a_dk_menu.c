/* nv2a_dk_menu.c -- the settings menu's core for the Deko3D build.
 *
 * This file is the menu's working half: the settings store (the file
 * beside options.txt, and options.txt itself where a launcher option
 * stands over the file), the row tables whose get/set are views of
 * those settings and of the engine's own state, the modal actions,
 * and the cheat / music / map state machines. Following the house
 * format in docs/menu-format.md: the same pages and the same rows
 * the GL menu has, over the same stored settings.
 *
 * The front-end is Dear ImGui (nv2a_dk_menu_imgui.cpp, rendered by
 * nv2a_dk_imgui_dk.cpp inside present()) -- the hand-drawn front-end
 * this file used to carry was retired with its renderer machinery;
 * nv2a_dk_menu_state.h is the contract between the two halves. The
 * core owns the state; the front-end renders it and calls back
 * through the dk_menu_* accessors at the bottom of this file.
 *
 * The pad arrives on the game's input thread (nv2a_dk_menu_input,
 * from xinput_device.c) and is latched; the front-end consumes the
 * latch on the renderer thread, so row changes happen there now.
 * Renderer-owned changes (scale, the frame counter, the music mix)
 * still cross through dk_ui_request_* and are applied by the
 * renderer at the next present. Everything else the rows touch is
 * atomic or volatile state owned by the engine pieces themselves,
 * exactly as the GL menu's rows are.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <dirent.h>   /* the custom-track folder scan */

#include "nv2a_dk_menu.h"
#include "nv2a_dk_ui.h"
#include "nv2a_dk_menu_state.h"
#include "../jsrf_maps.h"

/* The engine pieces the rows drive. */
extern int  recomp_cheat_infinite_cans_state(void);
extern void recomp_cheat_infinite_cans_toggle(void);
extern void recomp_cheat_refill_cans(void);
extern int  recomp_cheat_teleport_ready(void);
extern int  recomp_cheat_teleport_position(float xyz[3]);
extern void recomp_cheat_teleport_request(float x, float y, float z);
extern int  recomp_map_jump_available(void);
extern void recomp_map_jump_request(uint32_t mission);
extern int  recomp_mission_redirect_state(void);
extern void recomp_mission_redirect_toggle(void);
extern int  xbox_music_mix_override(void);
extern void recomp_music_track_request(uint32_t bgm_id);
extern int  recomp_music_track_pending(void);
extern void recomp_music_track_cancel(void);
extern uint32_t recomp_music_now_id(void);
extern void recomp_custom_track_request(const char *leaf);
extern int  recomp_custom_track_state(void);
extern const char *recomp_custom_track_name(void);
extern const char *recomp_version(void);
extern int  nv2a_widescreen(void);
extern void nv2a_set_widescreen(int on);

/* ── The settings file and options.txt ───────────────────────────────
 * Faithful copies of the GL menu's three helpers (sdl_cfg_get/set and
 * switch_options_set in nv2a_gl.c), so a choice made in either build's
 * menu is stored the same way and read back by the other. The settings
 * file is the one JSRF_SETTINGS_FILE names; options.txt sits beside
 * it, and the launcher turns its lines into environment at the next
 * start, friendly names and raw RECOMP_/JSRF_ lines alike. */

static void cfg_path(char *buf, size_t n)
{
    const char *f = getenv("JSRF_SETTINGS_FILE");
    if (f && *f) { snprintf(buf, n, "%s", f); return; }
    buf[0] = 0;
}

static int cfg_get(const char *key, int def)
{
    char path[1024], line[1200];
    size_t kl = strlen(key);
    FILE *f;
    int v = def;
    cfg_path(path, sizeof path);
    if (!path[0] || !(f = fopen(path, "r"))) return def;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == '=') v = atoi(line + kl + 1);
    fclose(f);
    return v;
}

static void file_set(const char *path, const char *key, const char *val, int drop)
{
    char tmp[1100], line[1200];
    size_t kl = strlen(key);
    FILE *in, *out;
    int done = 0;
    if (!path[0]) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(out = fopen(tmp, "w"))) return;
    if ((in = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof line, in)) {
            if (!strncmp(line, key, kl) && line[kl] == '=') {
                if (!done && !drop) fprintf(out, "%s=%s\n", key, val);
                done = 1;
            } else fputs(line, out);
        }
        fclose(in);
    }
    if (!done && !drop) fprintf(out, "%s=%s\n", key, val);
    fclose(out);
    if (rename(tmp, path) != 0) { remove(path); rename(tmp, path); }
}

/* warn_get (the Cheats page's CHEAT WARNINGS row) is called by the
 * front-end on every frame that page is drawn, and cfg_get opens
 * and scans the settings file on every call. The value is cached
 * here instead; cfg_set drops the cache whenever the key is saved,
 * so a change still lands the moment it is made. */
static int s_warn_cache = -1;

static void cfg_set(const char *key, int val)
{
    char path[1024], v[16];
    cfg_path(path, sizeof path);
    snprintf(v, sizeof v, "%d", val);
    file_set(path, key, v, 0);
    if (!strcmp(key, "cheat_warnings")) s_warn_cache = -1;
}

/* options.txt: the file beside the settings file. val NULL drops the
 * line (a presence switch turned off, so the launcher will not set it
 * again from the file). */
static void options_set(const char *key, const char *val)
{
    char path[1100];
    const char *sf = getenv("JSRF_SETTINGS_FILE");
    char *slash;
    if (!sf || !*sf) return;
    snprintf(path, sizeof path, "%s", sf);
    slash = strrchr(path, '/');
    if (!slash) return;
    snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "options.txt");
    file_set(path, key, val ? val : "", val == NULL);
}

static void options_set_int(const char *key, int val)
{
    char v[16];
    snprintf(v, sizeof v, "%d", val);
    options_set(key, v);
}

/* A switch the engine reads from its environment, in the GL menu's
 * Developer-page shape: flipping sets the variable for this process
 * and rewrites the matching line of options.txt, which is where it
 * takes effect -- the next start, so the rows say (Restart). Presence
 * switches are unset rather than set to 0 when turned off, and their
 * line is dropped where the launcher would set it again. */
typedef struct { const char *var, *opt; int def, pres, drop; } DkSwitch;

static int switch_get(const DkSwitch *sw)
{
    const char *e = getenv(sw->var);
    return e ? (sw->pres ? 1 : atoi(e) != 0) : sw->def;
}

static void switch_flip(const DkSwitch *sw)
{
    int v = !switch_get(sw);
    if (sw->pres && !v) {
        unsetenv(sw->var);
        options_set(sw->opt, NULL);
    } else {
        char b[8];
        snprintf(b, sizeof b, "%d", v);
        setenv(sw->var, sw->pres ? "1" : b, 1);
        options_set(sw->opt, b);
    }
    fprintf(stderr, "[MENU] %s = %d, from the next start (%s in options.txt)\n",
            sw->var, v, sw->opt);
}

/* Put one switch back to a chosen state through the same flip (the
 * reset-all below rides it): a flip writes the variable and the
 * options.txt line, which is all a reset of a switch is. */
static void switch_reset(const DkSwitch *sw, int target)
{
    if (switch_get(sw) != target) switch_flip(sw);
}

/* The Developer page's engine switches: the ones shared code reads
 * (the GL-only ones -- CRC32 hashing, the widescreen FX pass, the
 * merge probe, overlap accounting, the GL shader cache, the GL
 * profiler -- are not here; a row that does nothing is worse than
 * no row. The [perf] summaries in this build's log come from the
 * platform layer's reporter thread, which runs unconditionally.) */
enum { SW_LOG, SW_SAMPLE, SW_LOGDIR, SW_SPIN, SW_FASTGPU,
       SW_MUTE, SW_CALLSNAP, SW_SEMLOG, SW_STICKBOTH,
       SW_FENCELOG, SW_WALKFIX, SW_TAILSCAN, SW_BULK, SW_NOCALL,
       SW_PBSCAN, SW_EAGER, SW_ACKPRIO, SW_FENCEMIRROR, SW_DKSERVER, SW_COUNT };
static const DkSwitch k_sw[SW_COUNT] = {
    [SW_LOG]       = { "JSRF_LOG",              "log",            1, 0, 0 },
    [SW_SAMPLE]    = { "JSRF_SAMPLE",           "sample",         0, 0, 0 },
    [SW_LOGDIR]    = { "JSRF_LOG_DIRECT",       "log_direct",     0, 0, 0 },
    [SW_SPIN]      = { "JSRF_SPIN_LOG",         "JSRF_SPIN_LOG",  0, 0, 0 },
    [SW_FASTGPU]   = { "RECOMP_NV2A_FLUSH_ACK", "fast_gpu",       0, 0, 0 },
    [SW_MUTE]      = { "RECOMP_MUTE",           "mute",           0, 0, 0 },
    [SW_CALLSNAP]  = { "RECOMP_PB_CALLSNAP",    "RECOMP_PB_CALLSNAP", 1, 0, 0 },
    [SW_SEMLOG]    = { "RECOMP_PB_SEMLOG",      "RECOMP_PB_SEMLOG",   0, 1, 1 },
    [SW_STICKBOTH] = { "RECOMP_MENU_STICK_BOTH","RECOMP_MENU_STICK_BOTH", 0, 1, 1 },
    [SW_FENCELOG]  = { "JSRF_FENCE_LOG",        "JSRF_FENCE_LOG",   0, 0, 0 },
    [SW_WALKFIX]   = { "RECOMP_PB_WALKFIX",     "RECOMP_PB_WALKFIX", 1, 0, 0 },
    [SW_TAILSCAN]  = { "RECOMP_PB_TAILSCAN",    "RECOMP_PB_TAILSCAN", 1, 0, 0 },
    [SW_BULK]      = { "RECOMP_PB_BULK",        "RECOMP_PB_BULK",     1, 0, 0 },
    [SW_NOCALL]    = { "RECOMP_PB_NOCALL",      "RECOMP_PB_NOCALL",   0, 1, 1 },
    [SW_PBSCAN]    = { "RECOMP_PB_SCAN",        "RECOMP_PB_SCAN",     0, 1, 1 },
    [SW_EAGER]     = { "RECOMP_PB_EAGER",       "RECOMP_PB_EAGER",    0, 0, 0 },
    [SW_ACKPRIO]   = { "RECOMP_NV2A_ACK_PRIO",  "RECOMP_NV2A_ACK_PRIO", 1, 0, 0 },
    /* This build's fence mirror defaults off: the launcher always
     * sets RECOMP_PB_EXEC, and the reader's no-variable answer is
     * "the executor writes the real values, so do not mirror". */
    [SW_FENCEMIRROR] = { "RECOMP_FENCE_MIRROR", "RECOMP_FENCE_MIRROR", 0, 0, 0 },
    [SW_DKSERVER]  = { "JSRF_DKSERVER",         "dkserver",       0, 0, 0 },
};

/* The Deko3D page's own switches, all read by the renderer at start. */
enum { DK_THREAD, DK_CACHED, DK_COMPRESS, DK_HEARTBEAT, DK_COMPILE,
       DK_MIPS, DK_BORDER, DK_FOG, DK_CULL, DK_POFF, DK_LIGHTING,
       DK_TEXGEN, DK_PIXALIGN, DK_SURFALPHA, DK_SW_COUNT };
static const DkSwitch k_dk_sw[DK_SW_COUNT] = {
    [DK_THREAD]    = { "RECOMP_DK_THREAD",     "RECOMP_DK_THREAD",     1, 0, 0 },
    [DK_CACHED]    = { "RECOMP_DK_DATA_CACHED","RECOMP_DK_DATA_CACHED",1, 0, 0 },
    [DK_COMPRESS]  = { "RECOMP_DK_COMPRESS",   "RECOMP_DK_COMPRESS",   1, 0, 0 },
    [DK_HEARTBEAT] = { "RECOMP_DK_HEARTBEAT",  "RECOMP_DK_HEARTBEAT",  0, 0, 0 },
    [DK_COMPILE]   = { "RECOMP_DK_COMPILE",    "RECOMP_DK_COMPILE",    1, 0, 0 },
    [DK_MIPS]      = { "RECOMP_DK_MIPS",       "RECOMP_DK_MIPS",       1, 0, 0 },
    [DK_BORDER]    = { "RECOMP_DK_BORDER",     "RECOMP_DK_BORDER",     1, 0, 0 },
    [DK_FOG]       = { "RECOMP_DK_FOG",        "RECOMP_DK_FOG",        1, 0, 0 },
    [DK_CULL]      = { "RECOMP_DK_CULL",       "RECOMP_DK_CULL",       1, 0, 0 },
    [DK_POFF]      = { "RECOMP_DK_POLY_OFFSET","RECOMP_DK_POLY_OFFSET",1, 0, 0 },
    [DK_LIGHTING]  = { "RECOMP_DK_LIGHTING",   "RECOMP_DK_LIGHTING",   1, 0, 0 },
    [DK_TEXGEN]    = { "RECOMP_DK_TEXGEN",     "RECOMP_DK_TEXGEN",     1, 0, 0 },
    [DK_PIXALIGN]  = { "RECOMP_DK_PIXEL_ALIGN","RECOMP_DK_PIXEL_ALIGN",1, 0, 0 },
    /* Presence, like the Stored Alpha reader itself: any value at
     * all restores the alpha writes, so off unsets and drops. */
    [DK_SURFALPHA] = { "RECOMP_DK_SURF_ALPHA", "RECOMP_DK_SURF_ALPHA", 0, 1, 1 },
};

/* ── The camera and layout preferences ───────────────────────────────
 * The GL build defines these in nv2a_gl.c; this build has no window
 * layer, so the menu provides them, with the same storage and the
 * same defaults the Switch GL build uses. recomp_manual.c and the
 * input layer call them every frame, so a change applies at once.
 * RECOMP_RSTICK_CAMERA, when set, still stands over the camera
 * choice -- that check is in recomp_manual.c itself. */

static volatile int s_rcam_mode = -1;   /* 0 off, 1 camera, 2 tracking */
static volatile int s_rcam_ease = -1;
static volatile int s_rcam_hold = -1;
static volatile int s_layout = -1;

int nv_window_pref_rstick_camera(void)
{
    if (s_rcam_mode < 0)
        s_rcam_mode = cfg_get("rstick_camera", 1)
            ? (cfg_get("rstick_tracking", 0) || cfg_get("rstick_locked", 0) ? 2 : 1) : 0;
    return s_rcam_mode;
}

int nv_window_pref_rstick_ease(void)
{
    if (s_rcam_ease < 0) s_rcam_ease = cfg_get("rstick_easeback", 0) != 0;
    return s_rcam_ease;
}

/* Stage 1 of the camera plan: while this is on, the ease back only
 * runs when the movement stick is also untouched (see recomp_manual.c).
 * This build's trial default is on; the GL build has no definition,
 * so its weak default keeps today's behaviour there. */
int nv_window_pref_rstick_holdview(void)
{
    if (s_rcam_hold < 0) s_rcam_hold = cfg_get("rstick_holdview", 1) != 0;
    return s_rcam_hold;
}

int nv_window_pref_nintendo_layout(void)
{
    if (s_layout < 0) {
        const char *e = getenv("RECOMP_NINTENDO_LAYOUT");
        s_layout = e ? atoi(e) != 0 : cfg_get("nintendo_layout", 0) != 0;
    }
    return s_layout;
}

static void rcam_set_mode(int mode)
{
    s_rcam_mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
    cfg_set("rstick_camera", s_rcam_mode != 0);
    cfg_set("rstick_tracking", s_rcam_mode == 2);
    cfg_set("rstick_locked", 0);
}

static void rcam_set_ease(int on)
{
    s_rcam_ease = on ? 1 : 0;
    cfg_set("rstick_easeback", s_rcam_ease);
}

static void rcam_set_hold(int on)
{
    s_rcam_hold = on ? 1 : 0;
    cfg_set("rstick_holdview", s_rcam_hold);
}

static void layout_set(int on)
{
    s_layout = on ? 1 : 0;
    cfg_set("nintendo_layout", s_layout);
    options_set_int("nintendo_layout", s_layout);   /* the env stands over the file */
    if (on) setenv("RECOMP_NINTENDO_LAYOUT", "1", 1);
    else unsetenv("RECOMP_NINTENDO_LAYOUT");
}

/* ── Modals ──────────────────────────────────────────────────────────
 * One modal at a time. A confirm carries an action and a parameter,
 * run by modal_confirm() when the player presses A. */
/* The values are the front-end's DK_ACT_* (nv2a_dk_menu_state.h). */
enum { ACT_NONE = DK_ACT_NONE, ACT_CHEAT_WARNING = DK_ACT_CHEAT_WARNING,
       ACT_MAP_JUMP = DK_ACT_MAP_JUMP, ACT_REDIRECT = DK_ACT_REDIRECT,
       ACT_REBUILD_PACK = DK_ACT_REBUILD_PACK, ACT_WARNINGS_OFF = DK_ACT_WARNINGS_OFF,
       ACT_RESET_ALL = DK_ACT_RESET_ALL };
static volatile int s_modal_action;
static volatile uint32_t s_modal_param;
static char s_modal_title[48];
static char s_modal_body[320];

static void modal_open(int action, uint32_t param, const char *title, const char *body)
{
    snprintf(s_modal_title, sizeof s_modal_title, "%s", title);
    snprintf(s_modal_body, sizeof s_modal_body, "%s", body);
    __atomic_store_n(&s_modal_param, param, __ATOMIC_RELEASE);
    __atomic_store_n(&s_modal_action, action, __ATOMIC_RELEASE);
}

static void modal_close(void) { __atomic_store_n(&s_modal_action, ACT_NONE, __ATOMIC_RELEASE); }

void xbox_MenuSetOpen(int on);   /* defined with the entry points, below */

/* ── Rows and pages ──────────────────────────────────────────────── */
/* The row model itself (DkRow, DkPage and the DK_R_* kinds) is
 * shared with the front-end: nv2a_dk_menu_state.h. */
enum { R_SECTION = DK_R_SECTION, R_INFO = DK_R_INFO, R_CHECK = DK_R_CHECK,
       R_CHOICE = DK_R_CHOICE, R_BUTTON = DK_R_BUTTON, R_SLIDER = DK_R_SLIDER };

#define SECTION(lbl)            { lbl, R_SECTION, NULL, 0, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL }
#define INFO(lbl, tip, fn)      { lbl, R_INFO, tip, 0, NULL, NULL, NULL, NULL, 0, fn, NULL, NULL, NULL }
#define CHECK(lbl, tip, g, s)   { lbl, R_CHECK, tip, 0, NULL, g, s, NULL, 0, NULL, NULL, NULL, NULL }
#define CHECKA(lbl, tip, g, s, a) { lbl, R_CHECK, tip, a, NULL, g, s, NULL, 0, NULL, NULL, NULL, NULL }
#define CHOICE(lbl, tip, g, s, ch) { lbl, R_CHOICE, tip, 0, NULL, g, s, ch, (int)(sizeof ch / sizeof ch[0]), NULL, NULL, NULL, NULL }
#define CHOICEA(lbl, tip, g, s, ch, a, en) { lbl, R_CHOICE, tip, a, en, g, s, ch, (int)(sizeof ch / sizeof ch[0]), NULL, NULL, NULL, NULL }
#define BUTTON(lbl, tip, r, en) { lbl, R_BUTTON, tip, 0, en, NULL, NULL, NULL, 0, NULL, NULL, NULL, r }
#define SLIDER(lbl, tip, gf, sf, en) { lbl, R_SLIDER, tip, 0, en, NULL, NULL, NULL, 0, NULL, gf, sf, NULL }

/* File */
static const char *file_build_value(intptr_t a, char *buf, size_t n)
{ (void)a; snprintf(buf, n, "%s", recomp_version()); return buf; }
static const char *file_renderer_value(intptr_t a, char *buf, size_t n)
{ (void)a; snprintf(buf, n, "THE SWITCH'S OWN GPU INTERFACE\nRENDER SCALE: %s", dk_ui_scale_name()); return buf; }
static const char *file_about_value(intptr_t a, char *buf, size_t n)
{ (void)a; snprintf(buf, n,
    "A FAN-MADE PORT OF JET SET RADIO FUTURE TO THE NINTENDO SWITCH.\n"
    "THE RENDERER IS ADAPTED FROM SKEAMMER'S JSRF-NX, WITH THANKS.\n"
    "NOT AFFILIATED WITH SEGA OR NINTENDO."); return buf; }
static void close_run(intptr_t a) { (void)a; xbox_MenuSetOpen(0); }
/* Reset every setting: the apply half lives with modal_confirm
 * (below the last row table, where every row's setter is in scope);
 * this half only asks, through the same modal pattern as the map
 * jumps and the cheat warning. */
static void reset_run(intptr_t a)
{ (void)a; modal_open(ACT_RESET_ALL, 0, "RESET ALL SETTINGS",
    "Put every setting on every page back to the value a fresh install starts with?\n\n"
    "Settings that apply live change at once; the ones marked (RESTART) take effect from the next start. Your save is not touched."); }
/* ── The legacy options files ──────────────────────────────────────
 * The launcher reads three files from the folder the settings file
 * lives in -- options.txt, then env.txt, then env, later files
 * winning (jsrf_switch.c's switch_env, whose folder is SWITCH_BASE:
 * the same folder, since the launcher sets JSRF_SETTINGS_FILE to
 * SWITCH_BASE "/settings"). env.txt and env are the names from
 * before options.txt existed; a stale one left on the card stands
 * over every change this menu writes to options.txt, so the menu
 * would show one value while the next start applies another.
 *
 * This row makes the trap visible: it is on the File page only
 * while such a file is there (the front-end does not draw an INFO
 * row whose enabled() says no). The probe mirrors the launcher
 * exactly -- the same folder, derived the way options_set derives
 * options.txt's, the same two names, tested the same way (an fopen
 * for reading) -- and it runs once: the launcher reads the files at
 * start, so one cannot begin to matter mid-session, and probing per
 * frame would be two file opens a frame for a fact that cannot
 * change. */
static int s_legacy_files = -1;   /* -1 unprobed; else 1 env.txt, 2 env, 3 both */
static int legacy_files(void)
{
    char path[1100];
    const char *sf;
    char *slash;
    FILE *f;
    if (s_legacy_files >= 0) return s_legacy_files;
    s_legacy_files = 0;
    sf = getenv("JSRF_SETTINGS_FILE");
    if (sf && *sf) {
        snprintf(path, sizeof path, "%s", sf);
        slash = strrchr(path, '/');
        if (slash) {
            snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "env.txt");
            if ((f = fopen(path, "r")) != NULL) { fclose(f); s_legacy_files |= 1; }
            snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "env");
            if ((f = fopen(path, "r")) != NULL) { fclose(f); s_legacy_files |= 2; }
        }
    }
    return s_legacy_files;
}
static int legacy_present(intptr_t a) { (void)a; return legacy_files() != 0; }
static const char *legacy_value(intptr_t a, char *buf, size_t n)
{
    int p = legacy_files();
    (void)a;
    if (p == 3)
        snprintf(buf, n, "env.txt AND env SIT BESIDE options.txt AND ARE READ AFTER IT, SO THEIR LINES WIN OVER THE MENU'S. IF A CHANGE HERE SEEMS TO BE IGNORED, DELETE BOTH FROM THE jsrf-unleashed FOLDER.");
    else if (p == 2)
        snprintf(buf, n, "env SITS BESIDE options.txt AND IS READ AFTER IT, SO ITS LINES WIN OVER THE MENU'S. IF A CHANGE HERE SEEMS TO BE IGNORED, DELETE env FROM THE jsrf-unleashed FOLDER.");
    else
        snprintf(buf, n, "env.txt SITS BESIDE options.txt AND IS READ AFTER IT, SO ITS LINES WIN OVER THE MENU'S. IF A CHANGE HERE SEEMS TO BE IGNORED, DELETE env.txt FROM THE jsrf-unleashed FOLDER.");
    return buf;
}
static const DkRow k_file_rows[] = {
    { "LEGACY OPTIONS FILE", R_INFO, "An options file from an older build is still on the card, and the launcher reads it after options.txt, so it wins over changes made in this menu. The text below names it. This row only appears while the file is there.", 0, legacy_present, NULL, NULL, NULL, 0, legacy_value, NULL, NULL, NULL },
    INFO("BUILD", "The build you are running. The letters after the version are the source revision it was made from.", file_build_value),
    INFO("RENDERER", "This build draws with the Switch's own GPU interface. The other build draws with OpenGL and has the full window menu; this menu is its equal in the renderer's own terms.", file_renderer_value),
    INFO("ABOUT", "Where this port comes from.", file_about_value),
    BUTTON("RESET ALL SETTINGS", "Put every setting on every page back to the value a fresh install starts with (the release options.txt and the settings the code starts from). Settings that apply live change at once; the ones marked (RESTART) take effect from the next start. A confirmation is asked first. Your save is not touched.", reset_run, NULL),
    BUTTON("CLOSE MENU", "Close the menu and go back to the game. The - button does the same.", close_run, NULL),
};

/* Display */
/* A hand-set fixed scale the list does not name (the renderer's
 * parser takes anything from 1.0 to 4.0) gets a trailing, honest
 * entry rather than dk_ui_scale_choice()'s rounding of it to a
 * neighbour. The entry exists only while such a value is
 * live; row_nchoices() is the count authority, as with the map rows. */
static char s_scale_custom[24] = "CUSTOM";
static const char *k_scale_choices[] = { "AUTO", "MAX", "1X", "1.5X", "2X", "3X", "4X", s_scale_custom };
static const char *k_scale_values[]  = { "auto", "max", "1", "1.5", "2", "3", "4", "" };
static int scale_custom(void)
{
    float f = dk_ui_scale_fixed();
    if (f < 0) return 0;
    return fabsf(f - 1.0f) > 0.01f && fabsf(f - 1.5f) > 0.01f && fabsf(f - 2.0f) > 0.01f
        && fabsf(f - 3.0f) > 0.01f && fabsf(f - 4.0f) > 0.01f;
}
static int  scale_get(intptr_t a)
{
    (void)a;
    if (scale_custom()) {
        snprintf(s_scale_custom, sizeof s_scale_custom, "CUSTOM (%gX)", (double)dk_ui_scale_fixed());
        return 7;
    }
    return dk_ui_scale_choice();
}
static void scale_set(intptr_t a, int i)
{
    (void)a;
    if (i == 7) {   /* re-selecting the custom entry keeps the value as it is */
        char b[16];
        snprintf(b, sizeof b, "%g", (double)dk_ui_scale_fixed());
        options_set("scale", b);
        return;
    }
    dk_ui_request_scale(i); options_set("scale", k_scale_values[i]);
}
static int  fps_get(intptr_t a) { (void)a; return dk_ui_fps_state(); }
static void fps_set(intptr_t a, int on)
{ (void)a; dk_ui_request_fps(on); options_set_int("show_fps", on); }
/* The counter's two extras, wired exactly as the counter itself:
 * a live request to the renderer and the options.txt line the
 * launcher maps back to the renderer's variable at the next start
 * (show_fps_ms -> RECOMP_DK_FPS_MS, show_fps_drw -> RECOMP_DK_FPS_DRW).
 * Both start off: the counter alone is the rate. */
static int  fpsms_get(intptr_t a) { (void)a; return dk_ui_fps_ms_state(); }
static void fpsms_set(intptr_t a, int on)
{ (void)a; dk_ui_request_fps_ms(on); options_set_int("show_fps_ms", on); }
static int  fpsdrw_get(intptr_t a) { (void)a; return dk_ui_fps_drw_state(); }
static void fpsdrw_set(intptr_t a, int on)
{ (void)a; dk_ui_request_fps_drw(on); options_set_int("show_fps_drw", on); }
/* The frame rate cap, the GL menu's row in this renderer's terms:
 * fps= in options.txt (the launcher maps it to RECOMP_FPS, default 60)
 * and a live request to the renderer, which enforces it between
 * presents. 0 is Unlimited, the GL convention. */
/* The cap itself takes any number -- the renderer's throttle
 * honours it as-is -- so a hand-set value outside the list is shown
 * as itself in a fourth entry that exists only while it is live,
 * never rounded to a neighbour (45 and 120 both showed as "60"). */
static char s_fpscap_custom[24] = "CUSTOM";
static const char *k_fpscap_choices[] = { "60", "30", "UNLIMITED", s_fpscap_custom };
static const int   k_fpscap_values[]  = { 60, 30, 0 };
static int s_fpscap_value = -1;
static int fpscap_value(void)
{
    if (s_fpscap_value < 0) {
        const char *e = getenv("RECOMP_FPS");
        s_fpscap_value = e ? atoi(e) : 60;
    }
    return s_fpscap_value;
}
static int fpscap_custom(void)
{
    int v = fpscap_value();
    return v > 0 && v != 60 && v != 30;
}
static int  fpscap_get(intptr_t a)
{
    int v = fpscap_value();
    (void)a;
    if (v <= 0) return 2;
    if (v == 30) return 1;
    if (v == 60) return 0;
    snprintf(s_fpscap_custom, sizeof s_fpscap_custom, "CUSTOM (%d)", v);
    return 3;
}
static void fpscap_set(intptr_t a, int i)
{
    (void)a;
    if (i == 3) {   /* re-selecting the custom entry keeps the value as it is */
        dk_ui_request_fps_cap(s_fpscap_value);
        options_set_int("fps", s_fpscap_value);
        return;
    }
    s_fpscap_value = k_fpscap_values[i];
    dk_ui_request_fps_cap(s_fpscap_value); options_set_int("fps", s_fpscap_value);
}
/* Vsync: the settings file's vsync=, with the RECOMP_VSYNC variable
 * standing over it, exactly the GL preference's shape. The renderer
 * reads the variable when it builds the swapchain, so a change is
 * restart-effective; the row keeps all three stores in step. */
static int  vsync_get(intptr_t a)
{
    const char *e = getenv("RECOMP_VSYNC");
    (void)a;
    return e ? atoi(e) != 0 : cfg_get("vsync", 1);
}
static void vsync_set(intptr_t a, int on)
{ (void)a; cfg_set("vsync", on); options_set_int("RECOMP_VSYNC", on); setenv("RECOMP_VSYNC", on ? "1" : "0", 1); }
/* The AUTO render scale's GPU budget (RECOMP_DK_GPU_MS): how much
 * GPU time a frame may take before Auto steps the internal
 * resolution down, and how far under it a frame must run before it
 * steps back up. The renderer reads it once, when the scale is set
 * up (scale_mode_read in nv2a_dk.c, which accepts 4-33 ms; 14 is
 * its default), so the row is honestly (RESTART). Wired exactly as
 * the Developer page's GPU slow-frame row is (gpu_get/gpu_set): the
 * stored value read back from the environment, a set writing the
 * variable and the raw options.txt line the launcher passes
 * through. The renderer takes any value in its range, so a
 * hand-set value outside the list is shown as itself in a
 * trailing CUSTOM entry that exists only while it is live, the
 * same pattern gpu_get uses. */
static char s_gpubudget_custom[24] = "CUSTOM";
static const char *k_gpubudget_choices[] = { "8 MS", "11 MS", "14 MS", "17 MS", "20 MS", s_gpubudget_custom };
static const int k_gpubudget_values[] = { 8, 11, 14, 17, 20 };
static int gpubudget_value(void)
{
    const char *e = getenv("RECOMP_DK_GPU_MS");
    return e ? atoi(e) : 14;
}
static int gpubudget_custom(void)
{
    int v = gpubudget_value(), i;
    for (i = 0; i < 5; i++)
        if (k_gpubudget_values[i] == v) return 0;
    return 1;
}
static int  gpubudget_get(intptr_t a)
{
    int v = gpubudget_value(), i;
    (void)a;
    for (i = 0; i < 5; i++)
        if (k_gpubudget_values[i] == v) return i;
    snprintf(s_gpubudget_custom, sizeof s_gpubudget_custom, "CUSTOM (%d MS)", v);
    return 5;
}
static void gpubudget_set(intptr_t a, int i)
{ char b[16]; int v = i == 5 ? gpubudget_value() : k_gpubudget_values[i];   /* the custom entry keeps its value */
  (void)a; snprintf(b, sizeof b, "%d", v);
  setenv("RECOMP_DK_GPU_MS", b, 1); options_set("RECOMP_DK_GPU_MS", b); }
static const DkRow k_display_rows[] = {
    SECTION("FRAME"),
    CHOICE("RENDER SCALE", "How much detail the picture is drawn with inside, before it is shown at the screen's size. Auto follows the screen; Max is the sharpest and the slowest. Applies at once and is kept in options.txt.", scale_get, scale_set, k_scale_choices),
    CHOICE("AUTO SCALE GPU BUDGET (RESTART)", "How much GPU time a frame may take before Auto render scale steps the picture down to hold the frame rate, and how far under it frames must run before it steps back up. Only matters while RENDER SCALE is AUTO. The renderer reads it when the scale is set up, so it applies from the next start. Kept in options.txt.", gpubudget_get, gpubudget_set, k_gpubudget_choices),
    CHOICE("FRAME RATE", "The fastest the picture may change, in frames per second; Unlimited lets the game run as fast as it can. Applies at once and is kept in options.txt.", fpscap_get, fpscap_set, k_fpscap_choices),
    CHECK("VSYNC (RESTART)", "Wait for the screen's refresh before showing each frame: on is smooth and tear-free; off shows each frame the moment it is ready, which can tear. The swapchain is built with it, so it applies from the next start. Kept in the settings file and options.txt.", vsync_get, vsync_set),
    CHECK("SHOW FRAME RATE", "A small counter in the corner of the picture: frames shown each second. SHOW FRAME TIME (MS) and SHOW DRAWS (DRW/F), the two rows under this one, can each add a figure under the number. Applies at once and is kept in options.txt.", fps_get, fps_set),
    CHECK("SHOW FRAME TIME (MS)", "Add the longest gap between two frames, in milliseconds, under the frame rate counter's number: a stutter shows here even while the rate reads high. It appears only while SHOW FRAME RATE is on. Applies at once and is kept in options.txt.", fpsms_get, fpsms_set),
    CHECK("SHOW DRAWS (DRW/F)", "Add the draws in each frame, averaged over the counter's half second, under the frame rate counter's number: the heavier a scene is to draw, the higher it reads. It appears only while SHOW FRAME RATE is on. Applies at once and is kept in options.txt.", fpsdrw_get, fpsdrw_set),
};

/* Graphics */
static int  wide_get(intptr_t a) { (void)a; return nv2a_widescreen(); }
static void wide_set(intptr_t a, int on)
{ (void)a; nv2a_set_widescreen(on); setenv("RECOMP_WIDESCREEN", on ? "1" : "0", 1);
  cfg_set("widescreen", on); options_set_int("widescreen", on); }
static int  stretch_get(intptr_t a) { (void)a; return dk_ui_stretch(); }
static void stretch_set(intptr_t a, int on)
{ (void)a; dk_ui_set_stretch(on);
  if (on) { setenv("JSRF_WIDE_STRETCH", "1", 1); options_set("JSRF_WIDE_STRETCH", "1"); }
  else { unsetenv("JSRF_WIDE_STRETCH"); options_set("JSRF_WIDE_STRETCH", NULL); } }
static const char *k_aniso_choices[] = { "OFF", "2X", "4X", "8X", "16X" };
static const char *k_aniso_values[]  = { "0", "2", "4", "8", "16" };
/* The stored choice (RECOMP_DK_ANISO in options.txt, exported to the
 * environment by the launcher), resolved once. The renderer's sampler
 * index is only the fallback for "never set": it latches at first use,
 * so reading it back after a change would show the old level for the
 * rest of the session while options.txt already holds the new one. */
static int s_aniso_choice = -1;
static int  aniso_get(intptr_t a)
{
    (void)a;
    if (s_aniso_choice < 0) {
        const char *e = getenv("RECOMP_DK_ANISO");
        int i;
        s_aniso_choice = dk_ui_aniso_index();
        if (e)
            for (i = 0; i < 5; i++)
                if (!strcmp(e, k_aniso_values[i])) { s_aniso_choice = i; break; }
    }
    return s_aniso_choice;
}
static void aniso_set(intptr_t a, int i)
{ (void)a; s_aniso_choice = i; setenv("RECOMP_DK_ANISO", k_aniso_values[i], 1); options_set("RECOMP_DK_ANISO", k_aniso_values[i]); }
static const DkRow k_graphics_rows[] = {
    SECTION("PICTURE"),
    CHECK("WIDESCREEN", "On a 16:9 screen, widen the view to fill it instead of drawing the game's 4:3 picture. Applies at once; kept in the settings file and options.txt.", wide_get, wide_set),
    CHECK("STRETCH TO FIT SCREEN", "Fill the whole screen with the 4:3 picture, stretched, instead of widening the view. Mutually exclusive with widescreen in effect: with stretch on, the game is not widened. Applies at once; kept in options.txt.", stretch_get, stretch_set),
    CHOICE("TEXTURE ANISOTROPY (RESTART)", "Sharper textures at glancing angles, up to the chosen level. The renderer's samplers are built with it, so it applies from the next start. Kept in options.txt. Your options.txt aniso choice is honoured until you change this.", aniso_get, aniso_set, k_aniso_choices),
};

/* Controller */
static const char *k_rcam_choices[] = { "OFF", "CAMERA", "CAMERA (TRACKING)" };
static int  rcam_get(intptr_t a) { (void)a; return nv_window_pref_rstick_camera(); }
static void rcam_set(intptr_t a, int i) { (void)a; rcam_set_mode(i); }
static int  ease_get(intptr_t a) { (void)a; return nv_window_pref_rstick_ease(); }
static void ease_set(intptr_t a, int on) { (void)a; rcam_set_ease(on); }
static int  hold_get(intptr_t a) { (void)a; return nv_window_pref_rstick_holdview(); }
static void hold_set(intptr_t a, int on) { (void)a; rcam_set_hold(on); }
static int  layout_get(intptr_t a) { (void)a; return nv_window_pref_nintendo_layout(); }
static void layout_set_row(intptr_t a, int on) { (void)a; layout_set(on); }
static int  sw_get(intptr_t a) { return switch_get(&k_sw[a]); }
static void sw_set(intptr_t a, int on) { (void)on; switch_flip(&k_sw[a]); }
static const DkRow k_controller_rows[] = {
    SECTION("CAMERA"),
    CHOICE("RIGHT-STICK CAMERA", "Off: the right stick is the game's own (its first-person look). Camera: the stick turns the view and the skater keeps his heading. Tracking: turning the view turns the skater with it. Applies at once; kept in the settings file.", rcam_get, rcam_set, k_rcam_choices),
    CHECK("CAMERA EASES BACK WHEN RELEASED", "After you let go of the right stick, the view drifts back behind the skater. Off on the Switch by default: the held view is the point, and the L trigger already resets the turn. Kept in the settings file.", ease_get, ease_set),
    CHECK("CAMERA HOLDS ITS VIEW", "With the ease back on, it only drifts home while the movement stick is also untouched: pan, let go of the right stick and keep skating, and the view stays where you put it. This build's camera trial; turn it off for the shipped behaviour. Kept in the settings file.", hold_get, hold_set),
    SECTION("PAD"),
    CHECK("NINTENDO LAYOUT (A/B, X/Y)", "The face buttons by their printed names: A does what A says, and B what B says. The game's own prompts are drawn for an Xbox pad and do not move. Applies at once; kept in the settings file and options.txt.", layout_get, layout_set_row),
    CHECKA("BOTH STICKS IN GAME MENUS (RESTART)", "The left and the right sticks both move through the game's own menus. Takes effect from the next start; kept in options.txt.", sw_get, sw_set, SW_STICKBOTH),
};

/* Cheats */
static volatile uint32_t s_tp_bits[3];
static volatile int s_tp_seeded;
static float tp_read(int axis)
{ uint32_t b = __atomic_load_n(&s_tp_bits[axis], __ATOMIC_ACQUIRE); float f; memcpy(&f, &b, 4); return f; }
static void tp_write(int axis, float f)
{ uint32_t b; memcpy(&b, &f, 4); __atomic_store_n(&s_tp_bits[axis], b, __ATOMIC_RELEASE); }
static void tp_seed(void)
{
    float xyz[3];
    if (recomp_cheat_teleport_position(xyz)) {
        tp_write(0, xyz[0]); tp_write(1, xyz[1]); tp_write(2, xyz[2]);
        __atomic_store_n(&s_tp_seeded, 1, __ATOMIC_RELEASE);
    }
}
static int  tp_ready(intptr_t a) { (void)a; return recomp_cheat_teleport_ready(); }
static float tp_getf(intptr_t axis) { return tp_read((int)axis); }
static void tp_setf(intptr_t axis, float f)
{ if (f > 10000.0f) f = 10000.0f; if (f < -10000.0f) f = -10000.0f; tp_write((int)axis, f); }
static int  cans_get(intptr_t a) { (void)a; return recomp_cheat_infinite_cans_state(); }
static void cans_set(intptr_t a, int on) { (void)a; (void)on; recomp_cheat_infinite_cans_toggle(); }
static void refill_run(intptr_t a) { (void)a; recomp_cheat_refill_cans(); }
static float s_bookmark[3];
static volatile int s_bookmark_ok;
static void teleport_run(intptr_t a)
{
    float xyz[3];
    (void)a;
    if (recomp_cheat_teleport_position(xyz)) {
        s_bookmark[0] = xyz[0]; s_bookmark[1] = xyz[1]; s_bookmark[2] = xyz[2];
        __atomic_store_n(&s_bookmark_ok, 1, __ATOMIC_RELEASE);
    }
    recomp_cheat_teleport_request(tp_read(0), tp_read(1), tp_read(2));
}
static int  bookmark_ok(intptr_t a) { (void)a; return __atomic_load_n(&s_bookmark_ok, __ATOMIC_ACQUIRE) && recomp_cheat_teleport_ready(); }
static void bookmark_run(intptr_t a)
{ (void)a; recomp_cheat_teleport_request(s_bookmark[0], s_bookmark[1], s_bookmark[2]); }
static int  warn_get(intptr_t a)
{ (void)a; if (s_warn_cache < 0) s_warn_cache = cfg_get("cheat_warnings", 1) != 0; return s_warn_cache; }
static void warn_set(intptr_t a, int on)
{ (void)a; if (on) cfg_set("cheat_warnings", 1);
  else modal_open(ACT_WARNINGS_OFF, 0, "WARNINGS OFF",
      "The cheat warning will not be shown again. The confirmations on map jumps remain.\n\nYou are on your own with progress-affecting cheats."); }

/* The map selector, by district (data: jsrf_maps.h, upstream 0.6.6). */
static const char *s_map_names[3][8];
static uint32_t s_map_missions[3][8];
static int s_map_counts[3], s_map_choice[3];
static const char *k_districts[3] = { "Shibuya-cho", "Kogane-cho", "Benten-cho" };
static void maps_init(void)
{
    int i, d;
    if (s_map_counts[0] || s_map_counts[1] || s_map_counts[2]) return;
    for (i = 0; i < JSRF_MAP_COUNT; i++)
        for (d = 0; d < 3; d++)
            if (!strcmp(k_jsrf_maps[i].district, k_districts[d]) && s_map_counts[d] < 8) {
                s_map_names[d][s_map_counts[d]] = k_jsrf_maps[i].name;
                s_map_missions[d][s_map_counts[d]] = k_jsrf_maps[i].mission;
                s_map_counts[d]++;
            }
    /* Defence in depth (the row counts are clamped in row_nchoices):
     * a slot with no map in it holds an empty name, never a NULL for
     * the draw to meet. The matching missions stay 0, which the
     * engine's jump request treats as "no jump". */
    for (d = 0; d < 3; d++)
        for (i = s_map_counts[d]; i < 8; i++)
            s_map_names[d][i] = "";
}
static int  map_get(intptr_t d) { return s_map_choice[d]; }
static void map_set(intptr_t d, int i)
{
    char body[200];
    if (i < 0 || i >= s_map_counts[d]) return;
    s_map_choice[d] = i;
    snprintf(body, sizeof body, "Jump to %s?\n\nThis loads a new area and can move your story progress. The game may offer to save it that way.",
             s_map_names[d][i]);
    modal_open(ACT_MAP_JUMP, s_map_missions[d][i], "MAP JUMP", body);
}
static int  map_avail(intptr_t a) { (void)a; return recomp_map_jump_available(); }
/* ── Custom tracks: scan storage ─────────────────────────────────────
 * The scan itself lives with the Music rows below, but row_nchoices
 * reads the count, so the storage is declared here. */
#define CUSTOM_DIR "sdmc:/switch/jsrf-unleashed/custom_tracks"
#define CUSTOM_MAX 64
#define CUSTOM_NAME_MAX 96
static char s_custom_leaf[CUSTOM_MAX][CUSTOM_NAME_MAX];
static const char *s_custom_names[CUSTOM_MAX];
static int s_custom_count, s_custom_invalid, s_custom_capped;
static volatile int s_custom_choice;

/* The district rows' choice arrays have room for 8 names, but only
 * s_map_counts[d] slots are filled; the rest are NULL. Seven more
 * rows carry a trailing CUSTOM entry that exists only while a
 * hand-set value outside the list is live (render scale, frame
 * rate, GPU slow-frame threshold, texture memory, texture check
 * interval, hitch threshold, auto scale GPU budget). Every other
 * CHOICE row's array
 * is fully populated, so its stored count is already right. All
 * count consumers must go through here. */
static int gpu_get(intptr_t a);   /* with the Developer rows, below */
static int gpu_custom(void);
static int texmb_get(intptr_t a);
static int texmb_custom(void);
static int texevery_get(intptr_t a);
static int texevery_custom(void);
static int hitch_get(intptr_t a);
static int hitch_custom(void);
static int row_nchoices(const DkRow *r)
{
    if (r->choices == s_map_names[0] || r->choices == s_map_names[1] ||
        r->choices == s_map_names[2])
        return s_map_counts[r->arg];
    if (r->choices == s_custom_names)
        return s_custom_count;
    if (r->choices == k_scale_choices) return scale_custom() ? 8 : 7;
    if (r->choices == k_fpscap_choices) return fpscap_custom() ? 4 : 3;
    if (r->get == gpu_get) return gpu_custom() ? 5 : 4;
    if (r->get == gpubudget_get) return gpubudget_custom() ? 6 : 5;
    if (r->get == texmb_get) return texmb_custom() ? 4 : 3;
    if (r->get == texevery_get) return texevery_custom() ? 5 : 4;
    if (r->get == hitch_get) return hitch_custom() ? 6 : 5;
    return r->nchoices;
}
static const DkRow k_cheat_rows[] = {
    SECTION("WARNINGS"),
    CHECK("CHEAT WARNINGS", "Show the warning when the Cheats page is opened: back up your save, or use a separate save for cheat runs. Turn it off and the warning stays away; the confirmations on map jumps remain either way.", warn_get, warn_set),
    SECTION("SESSION CHEATS"),
    CHECK("INFINITE SPRAY CANS", "Spray cans are topped up as you play. This session only; nothing is written to the save.", cans_get, cans_set),
    BUTTON("REFILL SPRAY CANS", "Top spray cans up to 10, once. This session only.", refill_run, tp_ready),
    SECTION("TELEPORT"),
    { "TELEPORT X", R_SLIDER, "The east-west coordinate the Teleport button moves you to. Seeded from where you are standing; left and right adjust, hold to go faster.", 0, tp_ready, NULL, NULL, NULL, 0, NULL, tp_getf, tp_setf, NULL },
    { "TELEPORT Y", R_SLIDER, "The height the Teleport button moves you to. Seeded from where you are standing; left and right adjust, hold to go faster.", 1, tp_ready, NULL, NULL, NULL, 0, NULL, tp_getf, tp_setf, NULL },
    { "TELEPORT Z", R_SLIDER, "The north-south coordinate the Teleport button moves you to. Seeded from where you are standing; left and right adjust, hold to go faster.", 2, tp_ready, NULL, NULL, NULL, 0, NULL, tp_getf, tp_setf, NULL },
    BUTTON("TELEPORT TO COORDINATES", "Move to the coordinates set above, on the next game update. Your position before the jump is bookmarked, so Return can undo it. Live memory only; the save is never touched.", teleport_run, tp_ready),
    BUTTON("RETURN TO BOOKMARK", "Go back to where the last teleport started from.", bookmark_run, bookmark_ok),
    SECTION("MAP JUMPS (PROGRESS)"),
    CHOICEA("SHIBUYA-CHO", "Jump to a free-skate area in Shibuya-cho. Loads a new area; can move story progress.", map_get, map_set, s_map_names[0], 0, map_avail),
    CHOICEA("KOGANE-CHO", "Jump to a free-skate area in Kogane-cho. Loads a new area; can move story progress.", map_get, map_set, s_map_names[1], 1, map_avail),
    CHOICEA("BENTEN-CHO", "Jump to a free-skate area in Benten-cho. Loads a new area; can move story progress.", map_get, map_set, s_map_names[2], 2, map_avail),
};

/* Music */
static const char *k_music_names[] = {
    "USE AREA MIX", "DJ MIX 01A", "DJ MIX 01B", "DJ MIX 02A", "DJ MIX 02B",
    "DJ MIX 03A", "DJ MIX 03B", "DJ MIX 04A", "DJ MIX 04B", "DJ MIX 05A",
    "DJ MIX 05B", "DJ MIX 06A", "DJ MIX 06B", "DJ MIX 07A", "DJ MIX 07B",
    "DJ MIX 08A", "DJ MIX 08B", "DJ MIX 09A", "DJ MIX 09B"
};
/* The staged pick: what the player is lining up. The row itself shows
 * the engine's override (xbox_music_mix_override), not this copy;
 * browsing (left/right, or moving in the list) only moves the stage,
 * and confirming the list commits it through dk_ui_request_music.
 * Seeded from the engine each time the menu opens. */
static volatile int s_music_choice;
static int  music_stage(void) { return __atomic_load_n(&s_music_choice, __ATOMIC_ACQUIRE); }
static int  music_get(intptr_t a)
{
    int v = xbox_music_mix_override();   /* engine truth, not the session copy */
    (void)a;
    return v < 0 ? 0 : v > 18 ? 18 : v;
}
static void music_set(intptr_t a, int i)   /* the commit: list confirm only */
{
    (void)a;
    if (i < 0) i = 0;
    if (i > 18) i = 18;
    __atomic_store_n(&s_music_choice, i, __ATOMIC_RELEASE);
    dk_ui_request_music(i);
}
static void music_stage_step(const DkRow *row, int d)
{
    int nc = row_nchoices(row), v = music_stage() + d;
    if (v < 0) v = nc - 1;
    else if (v >= nc) v = 0;
    __atomic_store_n(&s_music_choice, v, __ATOMIC_RELEASE);
}
static const char *music_status(intptr_t a, char *buf, size_t n)
{
    int live = music_get(a), st = music_stage();
    if (st < 0) st = 0;
    if (st > 18) st = 18;
    if (st == live)
        snprintf(buf, n, "NEXT STREAM: %s\nNOTHING STAGED.", k_music_names[live]);
    else
        snprintf(buf, n, "NEXT STREAM: %s\nSTAGED: %s - OPEN THE LIST AND SELECT IT TO APPLY IT.",
                 k_music_names[live], k_music_names[st]);
    return buf;
}
static const char *music_about(intptr_t a, char *buf, size_t n)
{ (void)a; snprintf(buf, n, "THE MIX APPLIES TO THE NEXT MUSIC STREAM THE GAME STARTS; THE TRACK ALREADY PLAYING IS NOT INTERRUPTED."); return buf; }

/* ── Tracks: on-demand play, by BGM table id ────────────────────────
 * The game's master BGM table (guest 0x1D3878) is one id namespace:
 * the 27 licensed songs of the garage picker (0x20-0x3A), the DJ sets
 * (0x3C-0x4B and set_04 at 0x1A) and the standalones share it. The
 * names below are the table's own file stems, so a pick is always a
 * real table entry -- including the three single-file sets the mix
 * row's formula cannot name (and its four picks that name files
 * which do not exist, mixes 7/8/11/12, are simply not here).
 * Confirming a pick latches its id with recomp_music_track_request;
 * the engine fires it through the game's own PlayBGM at the next
 * in-play moment, so the track changes now, not at the next stream
 * the game happens to start. The functional one-shots (clear,
 * g_over, the endings, the DJ demos) and grace (one file under two
 * ids, context unknown) are deliberately not offered. */
static const char *k_track_names[] = {
    /* the garage pool: the 27 licensed songs, table order */
    "ANSWER", "BABY T", "BIRTHDAY", "BOKFRESH", "BOUNCE", "BUTTRFLY",
    "CONCEPT", "DEALER", "FUTURE", "INTENT", "KOTO", "LATCHULA",
    "LETMOM", "LOVELOVE", "MIC", "MODEL", "OLDIES", "POOMPOOM",
    "ROCKITON", "SCRAPPY", "SNEAKMAN", "VICTORY", "WHATBOUT",
    "PATHETIC", "LIKEIT", "SWEET", "THATS",
    /* standalones */
    "TITLE", "G PARK", "AISLE10",
    /* the DJ sets, by set number */
    "SET 01A", "SET 01B", "SET 02A", "SET 02B", "SET 03A", "SET 03B",
    "SET 04", "SET 05A", "SET 05B", "S SET 06", "SET 06",
    "SET 07A", "SET 07B", "SET 08A", "SET 08B", "SET 09A", "SET 09B"
};
static const uint32_t k_track_ids[] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
    0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3A,
    0x00, 0x1E, 0x1F,
    0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x4B, 0x1A, 0x41, 0x42, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A
};
#define N_TRACKS ((int)(sizeof k_track_ids / sizeof k_track_ids[0]))
static const char *track_name_for_id(uint32_t id)
{
    int i;
    for (i = 0; i < N_TRACKS; i++) if (k_track_ids[i] == id) return k_track_names[i];
    return NULL;
}
static int track_index_for_id(uint32_t id)
{
    int i;
    for (i = 0; i < N_TRACKS; i++) if (k_track_ids[i] == id) return i;
    return -1;
}
/* The mix (file-override) index naming the same file as a set id, for
 * the 14 a/b sets the mix row's formula covers; -1 for anything else.
 * Mirrors kernel_file.c's formula in reverse (mix i -> s_set_%02d%c,
 * number (i+1)/2, 'a' odd / 'b' even) via the table ids. */
static int track_mix_for_id(uint32_t id)
{
    switch (id) {
    case 0x3C: return 1;  case 0x3D: return 2;  case 0x3E: return 3;
    case 0x3F: return 4;  case 0x40: return 5;  case 0x4B: return 6;
    case 0x41: return 9;  case 0x42: return 10; case 0x45: return 13;
    case 0x46: return 14; case 0x47: return 15; case 0x48: return 16;
    case 0x49: return 17; case 0x4A: return 18;
    default:   return -1;
    }
}
/* Staged/fired exactly like the mix pick: left/right and the list
 * browse move the stage, confirming the list commits -- here, by
 * latching the id for the engine's PlayBGM trigger. The row shows
 * the engine's current track (the sound manager's own bookkeeping),
 * falling back to the stage when the game is playing something the
 * list does not offer. Seeded from the engine each menu open. */
static volatile int s_track_choice;
static int  track_stage(void) { return __atomic_load_n(&s_track_choice, __ATOMIC_ACQUIRE); }
static int  track_get(intptr_t a)
{
    int i;
    (void)a;
    i = track_index_for_id(recomp_music_now_id());
    if (i >= 0) return i;
    i = track_stage();
    return i < 0 ? 0 : i >= N_TRACKS ? N_TRACKS - 1 : i;
}
static void track_set(intptr_t a, int i)   /* the commit: list confirm only */
{
    uint32_t id;
    int mix;
    (void)a;
    if (i < 0) i = 0;
    if (i >= N_TRACKS) i = N_TRACKS - 1;
    __atomic_store_n(&s_track_choice, i, __ATOMIC_RELEASE);
    id = k_track_ids[i];
    recomp_music_track_request(id);
    /* Keep the file-level override from rewriting the stream this
     * pick is about to open: a formula set keeps its own mix (so
     * later natural opens of the same set keep the pick), everything
     * else clears the override -- a song's or a single set's leaf
     * never matches the mix matcher's pattern, so nothing can
     * substitute it once the override is out of the way. */
    mix = track_mix_for_id(id);
    dk_ui_request_music(mix >= 0 ? mix : 0);
    fprintf(stderr, "[TRACK] menu pick %s (table id %u) latched; file override -> %s\n",
            k_track_names[i], id, mix >= 0 ? k_music_names[mix] : "USE AREA MIX");
}
static void track_stage_step(const DkRow *row, int d)
{
    int nc = row_nchoices(row), v = track_stage() + d;
    if (v < 0) v = nc - 1;
    else if (v >= nc) v = 0;
    __atomic_store_n(&s_track_choice, v, __ATOMIC_RELEASE);
}
static const char *track_status(intptr_t a, char *buf, size_t n)
{
    uint32_t now = recomp_music_now_id();
    const char *nn = now == 0xFFFFFFFFu ? NULL : track_name_for_id(now);
    int pend = recomp_music_track_pending(), st = track_stage();
    size_t off = 0;
    (void)a;
    if (st < 0) st = 0;
    if (st >= N_TRACKS) st = N_TRACKS - 1;
    if (now == 0xFFFFFFFFu)
        off += (size_t)snprintf(buf + off, n - off, "NOW: NOTHING SEEN YET THIS SESSION.");
    else if (nn)
        off += (size_t)snprintf(buf + off, n - off, "NOW: %s.", nn);
    else
        off += (size_t)snprintf(buf + off, n - off, "NOW: TABLE ID %u (NOT IN THE TRACK LIST).", now);
    if (pend >= 0) {
        const char *pn = track_name_for_id((uint32_t)pend);
        if (pn)
            snprintf(buf + off, n - off, "\nLATCHED: %s - PLAYS AT THE NEXT IN-PLAY MOMENT.", pn);
        else
            snprintf(buf + off, n - off, "\nLATCHED: TABLE ID %d - PLAYS AT THE NEXT IN-PLAY MOMENT.", pend);
    }
    else if (k_track_ids[st] != now)
        snprintf(buf + off, n - off, "\nSTAGED: %s - OPEN THE LIST AND SELECT IT TO PLAY IT.",
                 k_track_names[st]);
    else
        snprintf(buf + off, n - off, "\nNOTHING STAGED.");
    return buf;
}
static const char *track_about(intptr_t a, char *buf, size_t n)
{ (void)a; snprintf(buf, n, "A TRACK PLAYS THROUGH THE GAME'S OWN MUSIC CHANGER, SO IT STARTS AS SOON AS THE GAME IS IN PLAY AND THE GAME'S OWN BOOKKEEPING AGREES WITH WHAT YOU HEAR. IT LASTS UNTIL THE GAME NEXT CHANGES THE MUSIC ITSELF (AN AREA CHANGE, A MISSION), LIKE ANY TRACK THE GAME STARTS."); return buf; }
static double menu_now(void);   /* defined with the Developer rows, below */
/* A shuffle over the same table the PLAY A TRACK row lists, through
 * the same commit -- latch, override reconciliation and the [TRACK]
 * log line all included. The seed is the menu core's own clock, in
 * milliseconds at the moment of the press. */
static void track_random_run(intptr_t a)
{
    (void)a;
    track_set(0, (int)(menu_now() * 1000.0) % N_TRACKS);
}
static void track_cancel_run(intptr_t a)
{ (void)a; recomp_music_track_cancel(); }
static int  track_pending(intptr_t a)
{ (void)a; return recomp_music_track_pending() >= 0; }

/* ── Custom tracks: user-supplied .adx files ─────────────────────────
 * The app dir's custom_tracks folder, scanned each time the menu
 * opens (at the seed point in xbox_MenuSetOpen, with the mix and
 * track seeds). A file is listed only if it passes a real ADX header
 * check: the game decodes ADX itself and raises its disc-error
 * screen on a stream it cannot parse, so a renamed MP3 must never
 * reach it -- the scan is the feature's load-bearing wall, not a
 * courtesy. Confirming a pick latches the leaf name with
 * recomp_custom_track_request; the engine fires it on the SET 01A
 * carrier stream through the kernel's one-shot host redirect (see
 * recomp_manual.c and kernel_file.c). The scan's storage is declared
 * up by row_nchoices, which reads the count. */
/* CUSTOMSCAN-BEGIN -- the scan functions (libc only, over the storage
 * declared above row_nchoices): the host unit test extracts this span
 * verbatim, declares the same storage, and runs it against a
 * synthetic folder. */
static int custom_name_is_adx(const char *name)
{
    size_t len = strlen(name);
    const char *e;
    if (len <= 4) return 0;
    e = name + len - 4;
    return e[0] == '.'
        && (e[1] == 'a' || e[1] == 'A')
        && (e[2] == 'd' || e[2] == 'D')
        && (e[3] == 'x' || e[3] == 'X');
}
/* The ADX header is self-describing and big-endian: signature 0x8000,
 * encoding type (0x03 standard ADPCM, 0x04 ADPCM with scale; 0x10 and
 * up are AHX, a different codec), then version, channel count, sample
 * rate and total samples. Anything else is not a file the game's
 * decoder can be handed. */
static int custom_adx_header_valid(const unsigned char *hdr, size_t rd, long total)
{
    uint32_t rate, samples;
    if (total < 64 || rd < 32) return 0;
    if (hdr[0] != 0x80 || hdr[1] != 0x00) return 0;
    if (hdr[2] != 0x03 && hdr[2] != 0x04) return 0;
    if (hdr[6] != 3 && hdr[6] != 4) return 0;
    if (hdr[7] < 1 || hdr[7] > 2) return 0;
    rate = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16)
         | ((uint32_t)hdr[10] << 8) | (uint32_t)hdr[11];
    if (rate < 8000u || rate > 96000u) return 0;
    samples = ((uint32_t)hdr[12] << 24) | ((uint32_t)hdr[13] << 16)
            | ((uint32_t)hdr[14] << 8) | (uint32_t)hdr[15];
    if (samples == 0) return 0;
    return 1;
}
static int custom_foldcmp(const void *a, const void *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (;;) {
        unsigned char cx = (*x >= 'A' && *x <= 'Z') ? (unsigned char)(*x - 'A' + 'a') : *x;
        unsigned char cy = (*y >= 'A' && *y <= 'Z') ? (unsigned char)(*y - 'A' + 'a') : *y;
        if (cx != cy || !cx) return (int)cx - (int)cy;
        x++; y++;
    }
}
static void custom_scan_dir(const char *dir)
{
    DIR *d;
    struct dirent *de;
    int i;
    s_custom_count = 0;
    s_custom_invalid = 0;
    s_custom_capped = 0;
    for (i = 0; i < CUSTOM_MAX; i++) s_custom_names[i] = "";
    d = opendir(dir);
    if (!d) return;
    while ((de = readdir(d)) != NULL) {
        char path[512];
        unsigned char hdr[32];
        FILE *f;
        size_t rd;
        long total;
        if (!custom_name_is_adx(de->d_name)) continue;
        if (strlen(de->d_name) >= CUSTOM_NAME_MAX) { s_custom_invalid++; continue; }
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        f = fopen(path, "rb");
        if (!f) { s_custom_invalid++; continue; }
        rd = fread(hdr, 1, sizeof hdr, f);
        total = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
        fclose(f);
        if (!custom_adx_header_valid(hdr, rd, total)) { s_custom_invalid++; continue; }
        if (s_custom_count >= CUSTOM_MAX) { s_custom_capped = 1; continue; }
        strcpy(s_custom_leaf[s_custom_count], de->d_name);
        s_custom_count++;
    }
    closedir(d);
    /* FAT readdir order is arbitrary; the list reads alphabetically,
     * like the track list above it. Names are re-pointed after the
     * sort because qsort moves the leaf storage they point into. */
    if (s_custom_count > 1)
        qsort(s_custom_leaf, (size_t)s_custom_count, CUSTOM_NAME_MAX, custom_foldcmp);
    for (i = 0; i < s_custom_count; i++) s_custom_names[i] = s_custom_leaf[i];
}
/* CUSTOMSCAN-END */
static void custom_scan(void)
{
    custom_scan_dir(CUSTOM_DIR);
    __atomic_store_n(&s_custom_choice, 0, __ATOMIC_RELEASE);
    fprintf(stderr, "[CUSTOM] scan: %d track(s) ready, %d skipped (not valid ADX)%s\n",
            s_custom_count, s_custom_invalid, s_custom_capped ? ", list capped" : "");
}
/* Staged/fired exactly like the track pick: left/right and the list
 * browse move the stage, confirming the list commits by latching the
 * leaf for the engine's carrier fire. */
static int  custom_stage(void) { return __atomic_load_n(&s_custom_choice, __ATOMIC_ACQUIRE); }
static int  custom_avail(intptr_t a) { (void)a; return s_custom_count > 0; }
static int  custom_get(intptr_t a)
{
    int i = custom_stage();
    (void)a;
    if (i < 0) i = 0;
    if (i >= s_custom_count) i = s_custom_count - 1;
    return i < 0 ? 0 : i;
}
static void custom_set(intptr_t a, int i)   /* the commit: list confirm only */
{
    (void)a;
    if (s_custom_count <= 0) return;
    if (i < 0) i = 0;
    if (i >= s_custom_count) i = s_custom_count - 1;
    __atomic_store_n(&s_custom_choice, i, __ATOMIC_RELEASE);
    recomp_custom_track_request(s_custom_leaf[i]);
    /* The kernel redirect serves this pick, not the mix formula:
     * keep the override out of the carrier stream's way, exactly as
     * a non-formula track pick does. */
    dk_ui_request_music(0);
    fprintf(stderr, "[CUSTOM] menu pick %s latched; file override -> USE AREA MIX\n",
            s_custom_leaf[i]);
}
static void custom_stage_step(const DkRow *row, int d)
{
    int nc = row_nchoices(row), v = custom_stage() + d;
    if (nc <= 0) return;
    if (v < 0) v = nc - 1;
    else if (v >= nc) v = 0;
    __atomic_store_n(&s_custom_choice, v, __ATOMIC_RELEASE);
}
static const char *custom_status(intptr_t a, char *buf, size_t n)
{
    int st = recomp_custom_track_state();
    const char *nm = recomp_custom_track_name();
    size_t off = 0;
    (void)a;
    if (s_custom_count > 0)
        off += (size_t)snprintf(buf + off, n - off, "FOLDER: %d CUSTOM TRACK(S) READY.", s_custom_count);
    else
        off += (size_t)snprintf(buf + off, n - off, "FOLDER: NO CUSTOM TRACKS. PUT .ADX FILES IN CUSTOM_TRACKS IN THE APP FOLDER.");
    if (s_custom_invalid > 0)
        off += (size_t)snprintf(buf + off, n - off, "\n%d FILE(S) SKIPPED: NOT VALID ADX.", s_custom_invalid);
    if (s_custom_capped)
        off += (size_t)snprintf(buf + off, n - off, "\nONLY THE FIRST %d FILES ARE LISTED.", CUSTOM_MAX);
    if (st == 1 && nm)
        snprintf(buf + off, n - off, "\nLATCHED: %s - PLAYS AT THE NEXT IN-PLAY MOMENT.", nm);
    else if (st == 2 && nm)
        snprintf(buf + off, n - off, "\nPLAYING: %s (THE GAME CALLS IT SET 01A).", nm);
    else
        snprintf(buf + off, n - off, "\nNOTHING LATCHED.");
    return buf;
}
static const char *custom_about(intptr_t a, char *buf, size_t n)
{ (void)a; snprintf(buf, n, "CUSTOM TRACKS ARE YOUR OWN .ADX FILES, READ FROM THE CUSTOM_TRACKS FOLDER IN THE APP FOLDER (SDMC:/SWITCH/JSRF-UNLEASHED/CUSTOM_TRACKS) EACH TIME THE MENU OPENS. ONLY REAL CRI ADX FILES ARE LISTED: THE GAME DECODES ADX ITSELF, AND A FILE THAT IS NOT ADX CAN RAISE ITS DISC ERROR, SO ANYTHING ELSE IS KEPT OUT. LOOPS COME FROM THE FILE'S OWN HEADER, SO CONVERT WITH LOOP POINTS SET (VGAUDIO ENCODES ADX AND WRITES THEM). A PICK RIDES ON THE GAME'S SET 01A STREAM, WHICH IS WHY THE GAME'S OWN MUSIC DISPLAY CALLS IT SET 01A WHILE YOUR FILE PLAYS."); return buf; }
static const DkRow k_music_rows[] = {
    SECTION("MUSIC"),
    CHOICE("DJ MIX (NEXT STREAM)", "Which of the game's DJ mixes plays, instead of each area's own. Use Area Mix gives the choice back to the game. Left and right stage a pick; open the list and select it to apply it. It takes over on the next music stream the game starts, never mid-track. This session only.", music_get, music_set, k_music_names),
    INFO("MIX STATUS", "What the next music stream will play, and what you have staged.", music_status),
    INFO("ABOUT THE MIX", "How the mix takes effect.", music_about),
    SECTION("TRACKS"),
    CHOICE("PLAY A TRACK", "Play one track now: the game's licensed songs, its standalones and every DJ set, from the game's own music table. Left and right stage a pick; open the list and select it to play it. It starts at the next in-play moment and runs until the game changes the music itself. Picking a track also resets the DJ mix above to USE AREA MIX; picking a set keeps that set as the mix. This session only.", track_get, track_set, k_track_names),
    BUTTON("RANDOM TRACK", "Play a track picked at random from the same list as Play a Track, through the same latch: it starts at the next in-play moment, and resets the DJ mix the same way a pick does. This session only.", track_random_run, NULL),
    BUTTON("CANCEL PENDING TRACK", "Take back a latched track pick before it plays. There is nothing to cancel once it has started; by then the game is simply playing it.", track_cancel_run, track_pending),
    INFO("TRACK STATUS", "What is playing now, and what you have staged or latched.", track_status),
    INFO("ABOUT TRACKS", "How a track pick takes effect.", track_about),
    SECTION("CUSTOM TRACKS"),
    CHOICEA("PLAY A CUSTOM TRACK", "Play one of your own .adx files from the custom_tracks folder in the app folder. Left and right stage a pick; open the list and select it to play it. It starts at the next in-play moment and runs until the game changes the music itself. Picking a custom track also resets the DJ mix to USE AREA MIX. This session only.", custom_get, custom_set, s_custom_names, 0, custom_avail),
    INFO("CUSTOM STATUS", "What the custom folder holds, and what you have latched.", custom_status),
    INFO("ABOUT CUSTOM TRACKS", "What a custom track is, and the rules its file has to follow.", custom_about),
    SECTION("AUDIO"),
    CHECKA("MUTE (RESTART)", "Silence ALL of the game's audio, music and sound effects alike. It applies from the next start: flipping it now changes nothing you hear this session, and the state shown is the one the next start will use. Kept in options.txt.", sw_get, sw_set, SW_MUTE),
};

/* Developer */
static double menu_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static const char *status_value(intptr_t a, char *buf, size_t n)
{
    /* A rolling window, not the GL menu's rates-between-looks: this
     * front-end calls value() on every frame the page is drawn, so a
     * between-looks baseline reset on every call and the rates never
     * showed. Keep the last couple of seconds of samples instead and
     * report against the oldest one at least a second back -- the
     * per-frame calls are exactly what a rolling window wants. */
    enum { SN = 24 };
    static double s_t[SN]; static unsigned s_f[SN], s_d[SN];
    static int s_head, s_count; static double s_last_add;
    double now = menu_now();
    unsigned frames = dk_ui_frames(), draws = dk_ui_draws();
    int i, old = -1;
    (void)a;
    if (!s_count || now - s_last_add >= 0.05) {
        s_t[s_head] = now; s_f[s_head] = frames; s_d[s_head] = draws;
        s_head = (s_head + 1) % SN;
        if (s_count < SN) s_count++;
        s_last_add = now;
    }
    for (i = s_count - 1; i >= 0; i--) {   /* oldest first; ages shrink as i falls */
        int idx = (s_head - 1 - i + SN * 2) % SN;
        double age = now - s_t[idx];
        if (age > 2.5) continue;      /* stale: from before a menu close */
        if (age >= 1.0) old = idx;    /* the oldest sample inside the window */
        break;
    }
    if (old < 0) {
        snprintf(buf, n, "Counting from now.\n\nThe rates appear here after a second of counting.");
        return buf;
    }
    {
        double dt = now - s_t[old];
        snprintf(buf, n, "%.0f FRAMES A SECOND\n%u DRAWS A SECOND\n\nOVER THE LAST %.1f SECONDS.",
                 (frames - s_f[old]) / dt, (unsigned)((draws - s_d[old]) / dt), dt);
    }
    return buf;
}
static int  redirect_get(intptr_t a) { (void)a; return recomp_mission_redirect_state(); }
static void redirect_set(intptr_t a, int on)
{ (void)a; (void)on; modal_open(ACT_REDIRECT, 0, "99TH STREET (TEST)",
    "Start in 99th Street at the title's hidden test stage instead of Shibuya Terminal?\n\nThe toggle is read when a stage loads."); }
static int  dksw_get(intptr_t a) { return switch_get(&k_dk_sw[a]); }
static void dksw_set(intptr_t a, int on) { (void)on; switch_flip(&k_dk_sw[a]); }
/* The renderer takes any threshold, so a hand-set value outside
 * the list is shown as itself in a fifth entry that exists only
 * while it is live (20 showed as "16 MS"). */
static char s_gpu_custom[24] = "CUSTOM";
static const char *k_gpu_choices[] = { "OFF", "8 MS", "16 MS", "33 MS", s_gpu_custom };
static const int k_gpu_values[] = { 0, 8, 16, 33 };
/* The renderer's live threshold once it has latched one (from the
 * environment at its first report, or from this row); the
 * environment until then. The row shows the state the engine is
 * actually in, not the file's. */
static int gpu_value(void)
{
    int live = dk_ui_gpu_slow_ms();
    const char *e;
    if (live >= 0) return live;
    e = getenv("RECOMP_DK_GPU_SLOW_MS");
    return e ? atoi(e) : 16;
}
static int gpu_custom(void)
{
    int v = gpu_value(), i;
    for (i = 0; i < 4; i++)
        if (k_gpu_values[i] == v) return 0;
    return 1;
}
static int  gpu_get(intptr_t a)
{
    int v = gpu_value(), i;
    (void)a;
    for (i = 0; i < 4; i++)
        if (k_gpu_values[i] == v) return i;
    snprintf(s_gpu_custom, sizeof s_gpu_custom, "CUSTOM (%d MS)", v);
    return 4;
}
static void gpu_set(intptr_t a, int i)
{ char b[16]; int v = i == 4 ? gpu_value() : k_gpu_values[i];   /* the custom entry keeps its value */
  (void)a; snprintf(b, sizeof b, "%d", v);
  dk_ui_request_gpu_slow_ms(v);
  setenv("RECOMP_DK_GPU_SLOW_MS", b, 1); options_set("RECOMP_DK_GPU_SLOW_MS", b); }
/* The [hitch] log's threshold (RECOMP_DK_HITCH_MS): which frames
 * earn a line. Wired exactly as the GPU row above: the getter reads
 * the renderer's live variable once it has latched, the setter
 * writes the live variable through the request door as well as the
 * environment and options.txt, so a flip lands at the next present.
 * The renderer takes any value of 17 or more (below that it falls
 * back to 50), so a hand-set value outside the list is shown as
 * itself in a trailing entry that exists only while it is live. */
static char s_hitch_custom[24] = "CUSTOM";
static const char *k_hitch_choices[] = { "17 MS", "22 MS", "25 MS", "33 MS", "50 MS", s_hitch_custom };
static const int k_hitch_values[] = { 17, 22, 25, 33, 50 };
static int hitch_value(void)
{
    int live = dk_ui_hitch_ms();
    const char *e;
    if (live > 0) return live;
    e = getenv("RECOMP_DK_HITCH_MS");
    return e && atoi(e) >= 17 ? atoi(e) : 50;
}
static int hitch_custom(void)
{
    int v = hitch_value(), i;
    for (i = 0; i < 5; i++)
        if (k_hitch_values[i] == v) return 0;
    return 1;
}
static int hitch_get(intptr_t a)
{
    int v = hitch_value(), i;
    (void)a;
    for (i = 0; i < 5; i++)
        if (k_hitch_values[i] == v) return i;
    snprintf(s_hitch_custom, sizeof s_hitch_custom, "CUSTOM (%d MS)", v);
    return 5;
}
static void hitch_set(intptr_t a, int i)
{ char b[16]; int v = i == 5 ? hitch_value() : k_hitch_values[i];   /* the custom entry keeps its value */
  (void)a; snprintf(b, sizeof b, "%d", v);
  dk_ui_request_hitch_ms(v);
  setenv("RECOMP_DK_HITCH_MS", b, 1); options_set("RECOMP_DK_HITCH_MS", b); }
/* How often a settled texture's contents are re-checked against the
 * game's memory (RECOMP_DK_TEX_EVERY, default every 4 frames). The
 * variable the check consults is the renderer's own, so the row is
 * live through the request door, like the two rows above. The
 * renderer takes any number from 1 up; a hand-set value outside
 * the list shows as itself. */
static char s_texevery_custom[24] = "CUSTOM";
static const char *k_texevery_choices[] = { "EVERY FRAME", "2 FRAMES", "4 FRAMES", "8 FRAMES", s_texevery_custom };
static const int k_texevery_values[] = { 1, 2, 4, 8 };
static int texevery_value(void)
{
    int live = dk_ui_tex_every();
    const char *e;
    if (live >= 1) return live;
    e = getenv("RECOMP_DK_TEX_EVERY");
    return e && atoi(e) >= 1 ? atoi(e) : 4;
}
static int texevery_custom(void)
{
    int v = texevery_value(), i;
    for (i = 0; i < 4; i++)
        if (k_texevery_values[i] == v) return 0;
    return 1;
}
static int texevery_get(intptr_t a)
{
    int v = texevery_value(), i;
    (void)a;
    for (i = 0; i < 4; i++)
        if (k_texevery_values[i] == v) return i;
    snprintf(s_texevery_custom, sizeof s_texevery_custom, "CUSTOM (%d)", v);
    return 4;
}
static void texevery_set(intptr_t a, int i)
{ char b[16]; int v = i == 4 ? texevery_value() : k_texevery_values[i];   /* the custom entry keeps its value */
  (void)a; snprintf(b, sizeof b, "%d", v);
  dk_ui_request_tex_every(v);
  setenv("RECOMP_DK_TEX_EVERY", b, 1); options_set("RECOMP_DK_TEX_EVERY", b); }
/* The texture memory heap's size (RECOMP_DK_TEX_MB, default 256):
 * how much GPU memory textures may hold before the oldest are
 * evicted. The heap is built once at start, so this row is
 * (RESTART): it writes the environment and options.txt only. The
 * renderer accepts 16 to 1024 and falls back to 256 outside it; a
 * hand-set value inside the range but outside the list shows as
 * itself, like the rows above. */
static char s_texmb_custom[24] = "CUSTOM";
static const char *k_texmb_choices[] = { "128 MB", "256 MB", "512 MB", s_texmb_custom };
static const int k_texmb_values[] = { 128, 256, 512 };
static int texmb_value(void)
{
    const char *e = getenv("RECOMP_DK_TEX_MB");
    int v = e ? atoi(e) : 256;
    return v >= 16 && v <= 1024 ? v : 256;
}
static int texmb_custom(void)
{
    int v = texmb_value(), i;
    for (i = 0; i < 3; i++)
        if (k_texmb_values[i] == v) return 0;
    return 1;
}
static int texmb_get(intptr_t a)
{
    int v = texmb_value(), i;
    (void)a;
    for (i = 0; i < 3; i++)
        if (k_texmb_values[i] == v) return i;
    snprintf(s_texmb_custom, sizeof s_texmb_custom, "CUSTOM (%d MB)", v);
    return 3;
}
static void texmb_set(intptr_t a, int i)
{ char b[16]; int v = i == 3 ? texmb_value() : k_texmb_values[i];   /* the custom entry keeps its value */
  (void)a; snprintf(b, sizeof b, "%d", v);
  setenv("RECOMP_DK_TEX_MB", b, 1); options_set("RECOMP_DK_TEX_MB", b); }
/* Depth clamping: per pixel in the rasterizer (the shipped
 * behaviour), or per vertex in the vertex shaders with
 * RECOMP_DK_ZCLAMP=vertex, as the renderer once did. A word, not a
 * number, so the switch helpers do not fit. The choice is latched
 * the first time a shader key is built, so the row is (RESTART). */
static int zclamp_get(intptr_t a)
{
    const char *e = getenv("RECOMP_DK_ZCLAMP");
    (void)a;
    return e && !strcmp(e, "vertex");
}
static void zclamp_set(intptr_t a, int on)
{
    (void)a;
    if (on) { setenv("RECOMP_DK_ZCLAMP", "vertex", 1); options_set("RECOMP_DK_ZCLAMP", "vertex"); }
    else { unsetenv("RECOMP_DK_ZCLAMP"); options_set("RECOMP_DK_ZCLAMP", NULL); }
    fprintf(stderr, "[MENU] RECOMP_DK_ZCLAMP = %s, from the next start (RECOMP_DK_ZCLAMP in options.txt)\n",
            on ? "vertex" : "off");
}
/* The live checks: the renderer's own state, consulted every frame
 * or every draw, so the flip lands at the next present through
 * dk_ui_request_*; each setter also keeps the environment and
 * options.txt in step, so the choice survives a restart. The
 * getters read the live state through the renderer, never the
 * file, so a row shows what the engine is actually doing. */
static int nodraw_get(intptr_t a) { (void)a; return dk_ui_nodraw(); }
static void nodraw_set(intptr_t a, int on)
{
    (void)a;
    dk_ui_request_nodraw(on);
    if (on) { setenv("RECOMP_DK_NODRAW", "1", 1); options_set("RECOMP_DK_NODRAW", "1"); }
    else { unsetenv("RECOMP_DK_NODRAW"); options_set("RECOMP_DK_NODRAW", NULL); }
}
static int trace_get(intptr_t a) { (void)a; return dk_ui_trace(); }
static void trace_set(intptr_t a, int on)
{
    (void)a;
    dk_ui_request_trace(on);
    if (on) { setenv("RECOMP_DK_TRACE", "1", 1); options_set("RECOMP_DK_TRACE", "1"); }
    else { unsetenv("RECOMP_DK_TRACE"); options_set("RECOMP_DK_TRACE", NULL); }
}
static int atprove_get(intptr_t a) { (void)a; return dk_ui_at_prove(); }
static void atprove_set(intptr_t a, int on)
{ (void)a; dk_ui_request_at_prove(on);
  setenv("RECOMP_DK_AT_PROVE", on ? "1" : "0", 1); options_set("RECOMP_DK_AT_PROVE", on ? "1" : "0"); }
static int ez_get(intptr_t a) { (void)a; return dk_ui_ez_test(); }
static void ez_set(intptr_t a, int on)
{ (void)a; dk_ui_request_ez_test(on);
  setenv("RECOMP_DK_EZ_TEST", on ? "1" : "0", 1); options_set("RECOMP_DK_EZ_TEST", on ? "1" : "0"); }
static void rebuild_run(intptr_t a)
{ (void)a; modal_open(ACT_REBUILD_PACK, 0, "REBUILD SHADER PACK",
    "Delete the compiled shader pack?\n\nThe next start recompiles every shader from the game first (the amber bar), which takes a while. This start is not affected."); }
static const DkRow k_dev_rows[] = {
    INFO("STATUS", "What the renderer has been doing: frames and draws a second, counted over the last second or so.", status_value),
    SECTION("LIVE SESSION"),
    CHECKA("DEBUG SERVER (RESTART)", "A TCP server on port 6543, for the development machine: the run's log as it is written, files under the app folder, controller presses injected into the game, and the port's own server commands (help lists them once connected). There is no password: anyone on the local network who can reach the port can use it, so leave it off unless you are testing. From the next start; kept in options.txt.", sw_get, sw_set, SW_DKSERVER),
    CHECK("START IN 99TH STREET", "At the title, load the hidden 99th Street test stage instead of Shibuya Terminal. Read when a stage loads; a confirmation is asked each time it is changed.", redirect_get, redirect_set),
    SECTION("LIVE COMPARE"),
    CHECK("DRAW NOTHING", "Count every draw and draw none of them: clears and presents keep running, so the screen shows the cleared frame while everything but the GPU drawing work keeps costing what it costs. Applies at once; kept in options.txt.", nodraw_get, nodraw_set),
    CHECK("RENDERER TRACE", "The renderer's trace in the log: surfaces and texture work as they happen, and a summary every 600 frames. Applies at once; kept in options.txt.", trace_get, trace_set),
    CHECK("ALPHA-TEST PROOF", "Skip the alpha test, and the extra shader pass behind it, on draws whose test is proved unable to fail: most of the cut-out textures. On is the normal way. Applies at once; kept in options.txt.", atprove_get, atprove_set),
    CHECK("NO ALPHA TEST (MEASUREMENT)", "Skip the alpha test on every draw, not just the proved ones: a measurement, not a fix. Cut-out edges are drawn solid while it is on. Applies at once; kept in options.txt.", ez_get, ez_set),
    CHOICE("TEXTURE CHECK EVERY", "How often a settled texture is checked against the game's memory. Every frame is the safest and the dearest; every 4 frames is the normal way. Applies at once; kept in options.txt.", texevery_get, texevery_set, k_texevery_choices),
    CHOICE("HITCH LOG THRESHOLD", "How slow a frame must be before the log names it a hitch and records what every thread was doing. 17 ms catches frames under about 60 fps; 50 ms is the normal way. Applies at once; kept in options.txt.", hitch_get, hitch_set, k_hitch_choices),
    CHOICE("GPU SLOW-FRAME LOG", "Log a line naming the targets of any frame whose GPU time passes the chosen threshold. Applies at once; kept in options.txt.", gpu_get, gpu_set, k_gpu_choices),
    SECTION("DIAGNOSTICS (RESTART)"),
    CHECKA("RUN LOG", "Keep the run's log file. From the next start; kept in options.txt.", sw_get, sw_set, SW_LOG),
    CHECKA("LOG DIRECT", "Write the log straight to the file instead of buffering it. From the next start; kept in options.txt.", sw_get, sw_set, SW_LOGDIR),
    CHECKA("SAMPLER", "Sample where the engine's threads spend their time (a CPU sampler: each thread's program counter is read every 2 ms, and the busiest addresses land in the log every 10 seconds). From the next start; kept in options.txt.", sw_get, sw_set, SW_SAMPLE),
    CHECKA("PUSHBUFFER CALL SNAPSHOT", "Snapshot pushbuffer calls in the profile. From the next start; kept in options.txt.", sw_get, sw_set, SW_CALLSNAP),
    CHECKA("SPIN LOG", "Log the guest's make-space spin poll, once a second. It does nothing unless FAST GPU (ENGINE section) is also on. From the next start; kept in options.txt.", sw_get, sw_set, SW_SPIN),
    CHECKA("GPU FENCE BLOCK LOG", "Dump the GPU fence block (its address and 24 words, raw) to the log once a second, so a run's semaphore state can be eyeballed. In this build that is all the switch does: the semaphore-release logging it is named for lives in the other pushbuffer executor, which this build does not use. From the next start; kept in options.txt.", sw_get, sw_set, SW_SEMLOG),
    CHECKA("FENCE WAIT LOG", "Log each time the game waits on a GPU fence: the value it waited for, how far the renderer had got, and how long the wait ran. From the next start; kept in options.txt.", sw_get, sw_set, SW_FENCELOG),
    SECTION("ENGINE (RESTART)"),
    CHECKA("FAST GPU", "Acknowledge the GPU's work without waiting for the full flush. From the next start; kept in options.txt.", sw_get, sw_set, SW_FASTGPU),
    CHECKA("RENDERER ON ITS OWN THREAD", "The renderer records GPU calls on its own core while the game thread walks the pushbuffer. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_THREAD),
    CHECKA("DATA RING CPU-CACHED", "The ring that carries vertices and textures to the GPU sits in CPU-cached memory. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_CACHED),
    CHECKA("COMPRESS RENDER TARGETS", "Render targets and depth buffers use the GPU's compression. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_COMPRESS),
    CHECKA("HEARTBEAT SQUARE", "A small square that moves every frame in the corner: if the picture hangs, it stops. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_HEARTBEAT),
    CHECKA("SHADER COMPILE AT START", "Compile the shader pack from the game when it is missing or stale. Off: the pack is only ever read, never built. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_COMPILE),
    CHECKA("MIP LEVELS", "Build the smaller copies of each texture that distant surfaces sample. Off sends every texture across at full size. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_MIPS),
    CHECKA("WHITE TEXTURE BORDER", "Frame a texture the game gives no border colour with a plain white pixel ring of its own, which stops dark seams at the edge of signs and stickers. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_BORDER),
    CHECKA("FOG", "The engine's fog: surfaces fade towards the fog colour with distance. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_FOG),
    CHECKA("FACE CULLING", "Skip drawing the faces that point away from the camera. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_CULL),
    CHECKA("POLYGON OFFSET", "Nudge decals and stickers a step towards the camera so they do not z-fight with the surface under them. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_POFF),
    CHECKA("FIXED-FUNCTION LIGHTING", "The Xbox's built-in lights: the game's lights and materials shaded the way the hardware did it. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_LIGHTING),
    CHECKA("TEXGEN AND MATRICES", "Texture coordinates the GPU generates itself (reflections, spheres) and the texture matrices that move them. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_TEXGEN),
    CHECKA("PIXEL-CENTRE ALIGN", "Shift drawn positions onto the image's pixel centres at an internal resolution, which keeps thin lines and text crisp. Off is how it drew before. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_PIXALIGN),
    CHECK("VERTEX DEPTH CLAMP", "Clamp depth per vertex in the vertex shaders instead of per pixel in the rasterizer, as the renderer did before the fix. Known to bring back the see-through characters it was fixed for; comparing only. From the next start; kept in options.txt.", zclamp_get, zclamp_set),
    CHECKA("STORED ALPHA", "Let drawing write the render target's alpha channel again, as it did before the fix. The boost dash's heat haze reads that alpha back, so off is the correct setting for playing; on is only for comparing the two. From the next start; kept in options.txt.", dksw_get, dksw_set, DK_SURFALPHA),
    CHOICE("TEXTURE MEMORY", "How much GPU memory textures may hold before the oldest are evicted to make room. From the next start; kept in options.txt.", texmb_get, texmb_set, k_texmb_choices),
    BUTTON("REBUILD SHADER PACK", "Delete dk_shaders.pak and dk_keys.bin so the next start recompiles every shader from the game. A confirmation is asked first.", rebuild_run, NULL),
    SECTION("EXECUTOR (RESTART)"),
    CHECKA("PB WALK FIX", "Walk a pushbuffer end marker's recorded size instead of believing the marker. Turning it off brings back the torn tail a run can end on, so only for comparing. From the next start; kept in options.txt.", sw_get, sw_set, SW_WALKFIX),
    CHECKA("PB TAIL SCAN", "Scan a pushbuffer's tail for the words its end marker missed, so nothing at the end of a buffer is lost. From the next start; kept in options.txt.", sw_get, sw_set, SW_TAILSCAN),
    CHECKA("PB BULK INDEX RUNS", "Copy long runs of index writes in one go instead of word by word. From the next start; kept in options.txt.", sw_get, sw_set, SW_BULK),
    CHECKA("PB SKIP CALLS", "Skip guest call values while scanning a pushbuffer instead of treating them as commands. From the next start; kept in options.txt.", sw_get, sw_set, SW_NOCALL),
    CHECKA("PB SURVEY", "Log pushbuffer shapes whole instead of walking them command by command: a survey tool for counting, not a playing mode. From the next start; kept in options.txt.", sw_get, sw_set, SW_PBSCAN),
    CHECKA("EAGER PB COPY", "Answer the game's GPU acknowledgement from the command word alone, the moment it changes, instead of after the walk. Off is the normal way. From the next start; kept in options.txt.", sw_get, sw_set, SW_EAGER),
    CHECKA("ACK THREAD PRIORITY", "Run the acknowledgement thread at top priority, so fence waits are answered as soon as the GPU state moves. From the next start; kept in options.txt.", sw_get, sw_set, SW_ACKPRIO),
    CHECKA("FENCE MIRROR", "Mirror the GPU semaphore values into guest memory from the acknowledgement thread. Off in this build: the executor writes the real values itself. From the next start; kept in options.txt.", sw_get, sw_set, SW_FENCEMIRROR),
};

enum { PG_FILE, PG_DISPLAY, PG_GRAPHICS, PG_CONTROLLER, PG_CHEATS, PG_MUSIC, PG_DEV, PG_COUNT };
static const DkPage k_pages[PG_COUNT] = {
    [PG_FILE]       = { "FILE",       k_file_rows,       (int)(sizeof k_file_rows / sizeof k_file_rows[0]) },
    [PG_DISPLAY]    = { "DISPLAY",    k_display_rows,    (int)(sizeof k_display_rows / sizeof k_display_rows[0]) },
    [PG_GRAPHICS]   = { "GRAPHICS",   k_graphics_rows,   (int)(sizeof k_graphics_rows / sizeof k_graphics_rows[0]) },
    [PG_CONTROLLER] = { "CONTROLLER", k_controller_rows, (int)(sizeof k_controller_rows / sizeof k_controller_rows[0]) },
    [PG_CHEATS]     = { "CHEATS",     k_cheat_rows,      (int)(sizeof k_cheat_rows / sizeof k_cheat_rows[0]) },
    [PG_MUSIC]      = { "MUSIC",      k_music_rows,      (int)(sizeof k_music_rows / sizeof k_music_rows[0]) },
    [PG_DEV]        = { "DEVELOPER",  k_dev_rows,        (int)(sizeof k_dev_rows / sizeof k_dev_rows[0]) },
};

/* ── State ───────────────────────────────────────────────────────────
 * Written on the input thread, read on the renderer thread. Plain
 * ints: a frame can at worst draw last frame's highlight. The modal
 * text is published before its action flag (release/acquire), and
 * the teleport coordinates travel as atomic float bits. */
static volatile int s_open;
static int s_page, s_prev_page;
static int s_row[PG_COUNT];         /* each page's remembered focus row */
static volatile int s_warned;       /* the cheat warning, this game session */
static volatile uint32_t s_buttons; /* the pad latch, from the input thread */

static const DkRow *page_rows(void) { return k_pages[s_page].rows; }
static int page_nrows(void) { return k_pages[s_page].nrows; }

static int row_focusable(const DkRow *r)
{
    if (r->kind == R_SECTION) return 0;
    if (r->enabled && !r->enabled(r->arg)) return 0;
    return 1;
}

static void snap_focus(void)
{
    const DkRow *rows = page_rows();
    int n = page_nrows(), i;
    if (row_focusable(&rows[s_row[s_page]])) return;
    for (i = 0; i < n; i++)
        if (row_focusable(&rows[i])) { s_row[s_page] = i; return; }
}

static void cheat_gate(void)
{
    if (!cfg_get("cheat_warnings", 1)) return;
    if (__atomic_load_n(&s_warned, __ATOMIC_ACQUIRE)) return;
    modal_open(ACT_CHEAT_WARNING, 0, "BEFORE YOU CHEAT",
        "Cheats change the game's live state. Most are session-only and vanish when the game closes, "
        "but map jumps can move your story progress, and the game may offer to save it that way.\n\n"
        "Back up your save first, or use a separate save for cheat runs.");
}

static void set_page(int p)
{
    if (p == s_page) return;
    s_prev_page = s_page;
    s_page = p;
    snap_focus();
    if (p == PG_CHEATS) {
        if (!__atomic_load_n(&s_tp_seeded, __ATOMIC_ACQUIRE)) tp_seed();
        cheat_gate();
    }
}

int xbox_MenuIsOpen(void)
{
    return __atomic_load_n(&s_open, __ATOMIC_ACQUIRE) != 0;
}

void xbox_MenuSetOpen(int on)
{
    if (on) {
        maps_init();
        __atomic_store_n(&s_music_choice, xbox_music_mix_override(), __ATOMIC_RELEASE);
        __atomic_store_n(&s_track_choice, track_get(0), __ATOMIC_RELEASE);
        custom_scan();
        if (!__atomic_load_n(&s_tp_seeded, __ATOMIC_ACQUIRE)) tp_seed();
        /* The pad latch restarts clean, so a button held across the
         * open reads as a level, never a stale edge. */
        __atomic_store_n(&s_buttons, 0, __ATOMIC_RELEASE);
        modal_close();
        snap_focus();
        __atomic_store_n(&s_open, 1, __ATOMIC_RELEASE);
        if (s_page == PG_CHEATS) cheat_gate();
    } else {
        __atomic_store_n(&s_open, 0, __ATOMIC_RELEASE);
        modal_close();
    }
}

/* ── Reset all settings ────────────────────────────────────────────
 * Every menu-managed setting back to the value a fresh install
 * starts with, through the rows' own setters (so each setting's
 * stores -- settings file, options.txt, environment, the renderer's
 * request latches -- are written exactly as if the player had set
 * the value by hand). The targets are the shipped state: the
 * release options.txt the build writes (tools/switch/build.sh's
 * write_default_options) where it speaks, and each getter's own
 * no-file fallback where it does not (the settings-file keys, the
 * Deko3D switches, the GPU slow-frame threshold). Session-only
 * state is not a setting and is left alone: the music mix and any
 * latched track, the spray-can cheat, the teleport coordinates and
 * bookmark. Lines in the shipped options.txt that this build's menu
 * has no row for (shader_cache, RECOMP_WIDE_FX_FULL,
 * RECOMP_GL_CRC32HASH -- GL-build settings) are likewise untouched. */
static void reset_apply(void)
{
    /* Display */
    scale_set(0, 2);        /* 1X: the shipped scale=1 */
    fpscap_set(0, 0);       /* 60 */
    vsync_set(0, 0);        /* the shipped RECOMP_VSYNC=0 stands over the file */
    fps_set(0, 0);          /* show_fps=0 */
    fpsms_set(0, 0);        /* show_fps_ms=0 */
    fpsdrw_set(0, 0);       /* show_fps_drw=0 */
    /* Graphics */
    wide_set(0, 1);         /* widescreen=1 */
    stretch_set(0, 1);      /* JSRF_WIDE_STRETCH is a line in the shipped file */
    aniso_set(0, 2);        /* 4X: the shipped aniso=4 */
    /* Controller */
    rcam_set_mode(1);       /* CAMERA */
    rcam_set_ease(0);
    rcam_set_hold(1);
    layout_set(0);          /* nintendo_layout=0 */
    switch_reset(&k_sw[SW_STICKBOTH], 0);
    /* Cheats */
    warn_set(0, 1);         /* cheat_warnings back on */
    /* Developer: game */
    if (recomp_mission_redirect_state()) recomp_mission_redirect_toggle();
    switch_reset(&k_sw[SW_MUTE], 0);
    /* Developer: engine (the shipped options.txt values) */
    switch_reset(&k_sw[SW_LOG], 0);
    switch_reset(&k_sw[SW_LOGDIR], 0);
    switch_reset(&k_sw[SW_SPIN], 0);
    switch_reset(&k_sw[SW_SAMPLE], 0);
    switch_reset(&k_sw[SW_FASTGPU], 1);
    switch_reset(&k_sw[SW_CALLSNAP], 1);
    switch_reset(&k_sw[SW_SEMLOG], 0);
    switch_reset(&k_sw[SW_FENCELOG], 0);
    /* Developer: executor (the switches' own defaults) */
    switch_reset(&k_sw[SW_WALKFIX], 1);
    switch_reset(&k_sw[SW_TAILSCAN], 1);
    switch_reset(&k_sw[SW_BULK], 1);
    switch_reset(&k_sw[SW_NOCALL], 0);
    switch_reset(&k_sw[SW_PBSCAN], 0);
    switch_reset(&k_sw[SW_EAGER], 0);
    switch_reset(&k_sw[SW_ACKPRIO], 1);
    switch_reset(&k_sw[SW_FENCEMIRROR], 0);
    switch_reset(&k_sw[SW_DKSERVER], 0);
    /* Developer: deko3d (the switches' own defaults) */
    switch_reset(&k_dk_sw[DK_THREAD], 1);
    switch_reset(&k_dk_sw[DK_CACHED], 1);
    switch_reset(&k_dk_sw[DK_COMPRESS], 1);
    switch_reset(&k_dk_sw[DK_HEARTBEAT], 0);
    switch_reset(&k_dk_sw[DK_COMPILE], 1);
    switch_reset(&k_dk_sw[DK_MIPS], 1);
    switch_reset(&k_dk_sw[DK_BORDER], 1);
    switch_reset(&k_dk_sw[DK_FOG], 1);
    switch_reset(&k_dk_sw[DK_CULL], 1);
    switch_reset(&k_dk_sw[DK_POFF], 1);
    switch_reset(&k_dk_sw[DK_LIGHTING], 1);
    switch_reset(&k_dk_sw[DK_TEXGEN], 1);
    switch_reset(&k_dk_sw[DK_PIXALIGN], 1);
    switch_reset(&k_dk_sw[DK_SURFALPHA], 0);
    zclamp_set(0, 0);
    texmb_set(0, 1);        /* texture memory: 256 MB, the renderer's fallback */
    gpu_set(0, 2);          /* GPU slow-frame log: 16 MS, gpu_get's fallback */
    /* Developer: live compare (the renderer's own defaults) */
    nodraw_set(0, 0);
    trace_set(0, 0);
    atprove_set(0, 1);
    ez_set(0, 0);
    texevery_set(0, 2);     /* texture check every 4 frames */
    hitch_set(0, 4);        /* hitch log threshold: 50 MS */
    fprintf(stderr, "[MENU] all settings reset to the shipped defaults\n");
}

/* ── Modals, resolved ────────────────────────────────────────────── */
static void modal_confirm(void)
{
    int action = __atomic_load_n(&s_modal_action, __ATOMIC_ACQUIRE);
    uint32_t param = __atomic_load_n(&s_modal_param, __ATOMIC_ACQUIRE);
    modal_close();
    switch (action) {
    case ACT_CHEAT_WARNING:
        __atomic_store_n(&s_warned, 1, __ATOMIC_RELEASE);
        break;
    case ACT_MAP_JUMP:
        recomp_map_jump_request(param);
        break;
    case ACT_REDIRECT:
        recomp_mission_redirect_toggle();
        break;
    case ACT_WARNINGS_OFF:
        cfg_set("cheat_warnings", 0);
        break;
    case ACT_REBUILD_PACK: {
        char path[1100];
        snprintf(path, sizeof path, "%s/dk_shaders.pak", dk_ui_data_dir());
        remove(path);
        snprintf(path, sizeof path, "%s/dk_keys.bin", dk_ui_data_dir());
        remove(path);
        fprintf(stderr, "[MENU] shader pack deleted; the next start recompiles it\n");
        break;
    }
    case ACT_RESET_ALL:
        reset_apply();
        break;
    default:
        break;
    }
}

static void modal_cancel(void)
{
    int action = __atomic_load_n(&s_modal_action, __ATOMIC_ACQUIRE);
    modal_close();
    if (action == ACT_CHEAT_WARNING)
        s_page = s_prev_page, snap_focus();   /* backed out: ask again next time */
}

/* ── Input ───────────────────────────────────────────────────────────
 * The pad state arrives as levels, polled on the input thread; the
 * bitmask is latched here and consumed by the front-end on the
 * renderer thread, which turns levels into ImGui key events (edges
 * and key repeat are ImGui's own, tuned to this menu's old cadence
 * via io.KeyRepeatDelay/Rate in nv2a_dk_menu_imgui.cpp). Opening the
 * menu clears the latch, so a button held across the open reads as
 * a level, never a stale edge. */

void nv2a_dk_menu_input(uint32_t buttons)
{
    __atomic_store_n(&s_buttons, buttons, __ATOMIC_RELEASE);
}


/* ── The front-end bridge ────────────────────────────────────────────
 * nv2a_dk_menu_draw is present()'s hook: while the menu is open it
 * hands the latched pad and the framebuffer size to the ImGui
 * front-end (nv2a_dk_menu_imgui.cpp). The accessors below are the
 * front-end's whole view of the core (nv2a_dk_menu_state.h): the
 * pages and the focus memory, the modal state, and the music
 * staging -- the core's tables and statics stay private. */

void nv2a_dk_menu_draw(void)
{
    if (!xbox_MenuIsOpen()) return;
    dk_imgui_menu_frame(__atomic_load_n(&s_buttons, __ATOMIC_ACQUIRE),
                        dk_ui_fb_w(), dk_ui_fb_h());
}

const DkPage *dk_menu_pages(int *count)
{
    if (count) *count = PG_COUNT;
    return k_pages;
}

int dk_menu_page(void) { return s_page; }

void dk_menu_set_page(int p)
{
    set_page(((p % PG_COUNT) + PG_COUNT) % PG_COUNT);
}

int dk_menu_focus_row(void) { return s_row[s_page]; }

void dk_menu_set_focus_row(int row)
{
    if (row >= 0 && row < page_nrows()) s_row[s_page] = row;
}

int dk_menu_row_nchoices(const DkRow *r) { return row_nchoices(r); }

unsigned dk_menu_row_flags(const DkRow *r)
{
    unsigned f = 0;
    if (r->set == music_set) f |= DK_ROW_F_MUSIC;
    if (r->set == track_set) f |= DK_ROW_F_TRACK;
    if (r->set == custom_set) f |= DK_ROW_F_CUSTOM;
    if (r->set == map_set) f |= DK_ROW_F_MAP;
    if (r->enabled == tp_ready) f |= DK_ROW_F_TPGATE;
    return f;
}

int dk_menu_tp_ready(void) { return recomp_cheat_teleport_ready(); }
int dk_menu_map_avail(void) { return recomp_map_jump_available(); }
int dk_menu_music_stage(void) { return music_stage(); }
void dk_menu_music_stage_step(const DkRow *r, int d) { music_stage_step(r, d); }
int dk_menu_track_stage(void) { return track_stage(); }
void dk_menu_track_stage_step(const DkRow *r, int d) { track_stage_step(r, d); }
int dk_menu_custom_stage(void) { return custom_stage(); }
void dk_menu_custom_stage_step(const DkRow *r, int d) { custom_stage_step(r, d); }

int dk_menu_modal_action(void)
{
    return __atomic_load_n(&s_modal_action, __ATOMIC_ACQUIRE);
}
const char *dk_menu_modal_title(void) { return s_modal_title; }
const char *dk_menu_modal_body(void) { return s_modal_body; }
void dk_menu_modal_confirm(void) { modal_confirm(); }
void dk_menu_modal_cancel(void) { modal_cancel(); }
