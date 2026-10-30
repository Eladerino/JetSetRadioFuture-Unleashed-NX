/*
 * menu_sdl.cpp -- the in-game menu over the SDL window: F1, or Back on a
 * controller (Game Mode on a handheld has no keyboard). Back is the menu's
 * own button: the input layer keeps the pad's Back from the game.
 *
 * Dear ImGui with its own SDL2 and OpenGL 3 backends, as they come
 * (third_party/imgui). The frame is built on the GL thread, which owns the
 * window -- SDL's mouse and keyboard calls want that thread -- and drawn by
 * whichever thread swaps: the GL thread, or on Windows the presenter, with a
 * context of its own (nv2a_gl.c). One mutex keeps the two out of ImGui at
 * the same time. Nothing runs, and nothing is drawn, while the menu is shut.
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as ImGui is:
 * nothing here may need the C++ runtime library (no new, no STL).
 *
 * Switch differences, marked __SWITCH__ below: the pad is polled by the
 * input layer rather than delivered as events, so that layer watches
 * Back (Minus) itself and calls xbox_MenuSetOpen, the event path's own
 * Back handling is compiled out (the same press would toggle twice),
 * and the pad's navigation reaches ImGui by a polled feed in
 * xbox_MenuFrame instead of by events.
 */
#include "menu_sdl.h"
#include "menu.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <stdio.h>
#include <string.h>

/* The GLSL ImGui's shaders are compiled as: the context the window asks for
 * (nv2a_gl.c's gl_init), not ImGui's default guess. */
#if defined(IMGUI_IMPL_OPENGL_ES3)
#  define OV_GLSL "#version 300 es"
#else
#  define OV_GLSL "#version 330 core"
#endif

/* The menu is sized as the game's own text is: by the window's height
 * against the Xbox's 480 lines. */
#define OV_GAME_LINES 480.0f

static SDL_mutex   *g_mu;
static volatile int g_ready;        /* both read by the input layer's thread */
static volatile int g_open;
static int          g_just_opened;
static float        g_scale;
static ImGuiStyle   g_style;        /* before scaling */
static const char  *g_about_gpu, *g_about_gl;
/* xbox_MenuBusy's box: set and built on the GL thread; the total is read by
 * whichever thread swaps, as g_open is. */
static const char  *g_busy_what;
static int          g_busy_done;
static volatile int g_busy_total;

/* ---- File: About, Restart, Exit ----------------------------------------- */

/* The engine's version comes from the port (port/main.c), which is built
 * with it; the runtime is not. */
extern "C" const char *recomp_version(void) __attribute__((weak));

/* File > About, a section at a time: what the project is and who it
 * credits, then the build, the OS, the graphics card, SDL, OpenGL, Dear
 * ImGui. Every section is written the same way, and so is any added later:
 * its name on a line of its own, then each detail -- the version first,
 * where it has one -- on a line starting with "-", and a blank line
 * between one section and the next (docs/menu-format.md). */
static const char *about_text(void)
{
    static char buf[2048];
    SDL_version built, running;
    SDL_VERSION(&built);
    SDL_GetVersion(&running);
    snprintf(buf, sizeof buf,
             "JSRF Unleashed NX\n"
             "-%s\n\n"
             "About\n"
             "-An unofficial fan port of Jet Set Radio Future\n"
             "-for the Nintendo Switch, rebuilt from the original\n"
             "-Xbox game by static recompilation. It needs your\n"
             "-own copy of the game to play, and is not affiliated\n"
             "-with or endorsed by SEGA.\n\n"
             "Credits\n"
             "-Jet Set Radio Future by Smilebit and SEGA\n"
             "-The Switch port of JetSetRadioFuture-Unleashed\n"
             "-by JuMatt, built on xboxrecomp by sp00nz and\n"
             "-the xemu project's NV2A and APU hardware models\n"
             "-The in-game menu, adapted from JSRF-PC\n"
             "-by spriteice\n\n"
             "%s\n"
             "-Built with %s\n\n"
             "Graphics\n"
             "-%s\n\n"
             "SDL\n"
             "-%u.%u.%u\n"
             "-Built against %u.%u.%u\n"
             "-%s video\n\n"
             "OpenGL\n"
             "-%s\n\n"
             "Dear ImGui\n"
             "-%s",
             recomp_version ? recomp_version() : "dev",
             SDL_GetPlatform(),
#if defined(__GNUC__) && !defined(__clang__)
             "GCC " __VERSION__,        /* GCC's is the bare number; Clang's names it */
#elif defined(__VERSION__)
             __VERSION__,
#else
             "an unknown compiler",
#endif
             g_about_gpu ? g_about_gpu : "unknown",
             running.major, running.minor, running.patch,
             built.major, built.minor, built.patch,
             SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "no",
             g_about_gl ? g_about_gl : "unknown",
             IMGUI_VERSION);
    return buf;
}

/* The game started again, as a new render scale starts it (nv2a_gl.c):
 * the host's own restart, which is what the wait-for-the-next-start
 * settings (the Developer's switches among them) apply through. Weak:
 * a host that cannot start itself again simply does not offer the
 * Restart row; the desktop host's is port/main.c's. */
extern "C" void recomp_restart_self(void) __attribute__((weak));

/* The window's close button: sdl_pump's SDL_QUIT, which reports and exits. */
static void exit_game(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_QUIT;
    SDL_PushEvent(&e);
}

/* Every field, in order: C++11 has no designated initializers. */
static const XBOX_MENU_ITEM k_file[] = {
    { XBOX_MENU_FILE, XBOX_ITEM_INFO,      "About", NULL, NULL,      NULL, NULL, 0, about_text, NULL, NULL, NULL, 0, 0, 0, NULL, 0 },
    { XBOX_MENU_FILE, XBOX_ITEM_SEPARATOR, NULL,    NULL, NULL,      NULL, NULL, 0, NULL,       NULL, NULL, NULL, 0, 0, 0, NULL, 0 },
    { XBOX_MENU_FILE, XBOX_ITEM_ACTION,    "Restart", NULL, recomp_restart_self, NULL, NULL, 0, NULL,
      "The game restarts, losing unsaved progress.", NULL, NULL, 0, 0, 0, NULL, 0 },
    { XBOX_MENU_FILE, XBOX_ITEM_ACTION,    "Exit",  NULL, exit_game, NULL, NULL, 0, NULL,
      "Unsaved progress is lost.", NULL, NULL, 0, 0, 0, NULL, 0 },
};

/* ---- opening and closing ------------------------------------------------ */

/* With g_mu held. */
static void set_open(int on)
{
    ImGuiIO &io = ImGui::GetIO();
    if (on == g_open) return;
    if (on) {
        /* Pads plugged in while the menu was shut were not seen: look again.
         * On the Switch the pad reaches ImGui through the polled feed
         * alone (switch_feed_nav), so the backend is told Manual, with no
         * gamepads of its own: its polling reads the buttons unswapped
         * and asserts them every frame, so with the Nintendo layout on
         * the two feeds press the swapped and unswapped face keys in the
         * same press -- an activate and a cancel together -- and no press
         * ever lands clean. */
#if defined(__SWITCH__)
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual);
#else
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);
#endif
        g_just_opened = 1;
    } else {
        /* What was held when it shut is let go of, not remembered. */
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    g_open = on;
    fprintf(stderr, "[MENU] %s\n", on ? "open" : "closed");
}

/* From outside the event path (the Switch's input layer): see menu_sdl.h. */
extern "C" void xbox_MenuSetOpen(int on)
{
    if (!g_ready) return;
    SDL_LockMutex(g_mu);
    set_open(on);
    SDL_UnlockMutex(g_mu);
}

/* ---- the calls from the window (nv2a_gl.c) ------------------------------ */

extern "C" int xbox_MenuInit(SDL_Window *window, void *gl_context, const char *about_gpu,
                             const char *about_gl)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;              /* nothing written beside the game */
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    /* The window keeps its own rule for the pointer (hidden in full screen). */
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.Fonts->AddFontDefaultVector();   /* the scalable one: see scale_to_window */
    ImGui::StyleColorsDark();
    g_style = ImGui::GetStyle();
    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context) || !ImGui_ImplOpenGL3_Init(OV_GLSL)) {
        fprintf(stderr, "[MENU] Dear ImGui did not start: no in-game menu\n");
        return 0;
    }
    g_mu = SDL_CreateMutex();
    if (!g_mu) return 0;
    g_about_gpu = about_gpu;
    g_about_gl = about_gl;
    /* Restart only where the game can start itself again, as Render
     * scale: the row is the one named so, and it goes in only when the
     * host provided recomp_restart_self. (Its callback is that same
     * weak symbol, so with no host restart it reads NULL, the same as
     * About's; the name is what tells the row apart.) */
    {
        XBOX_MENU_ITEM rows[sizeof k_file / sizeof k_file[0]];
        int n = 0;
        for (const XBOX_MENU_ITEM &it : k_file)
            if (!it.label || strcmp(it.label, "Restart") != 0 || recomp_restart_self) rows[n++] = it;
        xbox_MenuAdd(rows, n);
    }
    g_ready = 1;
#if defined(__SWITCH__)
    fprintf(stderr, "[MENU] in-game menu: Minus, tapped, held, or with L+R (Dear ImGui %s)\n",
            IMGUI_VERSION);
#else
    fprintf(stderr, "[MENU] in-game menu: F1, or Back on a controller (Dear ImGui %s)\n",
            IMGUI_VERSION);
#endif
    return 1;
}

#if defined(__SWITCH__)
/* The input layer polls Back (Minus) and toggles through xbox_MenuSetOpen;
 * an event for the same press arriving here would toggle a second time. */
#  define MENU_EV_BACK(ev) 0
#else
#  define MENU_EV_BACK(ev) \
    ((ev)->type == SDL_CONTROLLERBUTTONDOWN && (ev)->cbutton.button == SDL_CONTROLLER_BUTTON_BACK)
#endif

extern "C" int xbox_MenuEvent(const SDL_Event *ev)
{
    if (!g_ready) return 0;
    if ((ev->type == SDL_KEYDOWN && ev->key.keysym.scancode == SDL_SCANCODE_F1 && !ev->key.repeat)
     || MENU_EV_BACK(ev)) {
        SDL_LockMutex(g_mu);
        set_open(!g_open);
        SDL_UnlockMutex(g_mu);
        return 1;
    }
    if (!g_open) return 0;              /* shut: ImGui sees nothing, queues nothing */
    SDL_LockMutex(g_mu);
    ImGui_ImplSDL2_ProcessEvent(ev);
    SDL_UnlockMutex(g_mu);
    /* A key let go still reaches the game, so nothing it saw pressed stays
     * held; a key pressed, typed text and clicks are the menu's. Closing the
     * window, focus and the rest go on to sdl_pump as before. */
    return ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT
        || ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP
        || ev->type == SDL_MOUSEWHEEL;
}

static void scale_to_window(void)
{
    float s = ImGui::GetIO().DisplaySize.y / OV_GAME_LINES;
    if (s < 1.0f) s = 1.0f;
    if (s == g_scale) return;
    ImGuiStyle &st = ImGui::GetStyle();
    st = g_style;
    st.ScaleAllSizes(s);
    st.FontScaleMain = s;
    g_scale = s;
}

#if defined(__SWITCH__)
/* The window's Nintendo-layout preference (nv2a_gl), weak as the input
 * layer has it: with it on, the menu's own face buttons follow the
 * printed names too, or the menu would feel backwards in the hands of
 * the player who just turned the layout on. */
extern "C" int nv_window_pref_nintendo_layout(void) __attribute__((weak));

/* Pad navigation, fed from the polled controller: the input layer polls
 * the pad rather than taking its events, so the menu's navigation is fed
 * the same way, as ImGui key events on their edges, with the left stick
 * as analogue keys past a dead zone. Player one's pad; the first frame
 * the menu is open, and after a pad change, the handle is found again. */
static void switch_feed_nav(void)
{
    static SDL_GameController *pad;
    static unsigned prev;
    static const struct { SDL_GameControllerButton b; ImGuiKey k; } map[] = {
        { SDL_CONTROLLER_BUTTON_DPAD_UP,       ImGuiKey_GamepadDpadUp },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     ImGuiKey_GamepadDpadDown },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     ImGuiKey_GamepadDpadLeft },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    ImGuiKey_GamepadDpadRight },
        { SDL_CONTROLLER_BUTTON_A,             ImGuiKey_GamepadFaceDown },
        { SDL_CONTROLLER_BUTTON_B,             ImGuiKey_GamepadFaceRight },
        { SDL_CONTROLLER_BUTTON_X,             ImGuiKey_GamepadFaceLeft },
        { SDL_CONTROLLER_BUTTON_Y,             ImGuiKey_GamepadFaceUp },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  ImGuiKey_GamepadL1 },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, ImGuiKey_GamepadR1 },
        { SDL_CONTROLLER_BUTTON_START,         ImGuiKey_GamepadStart },
    };
    ImGuiIO &io = ImGui::GetIO();
    unsigned cur = 0;
    int i;
    if (!pad || !SDL_GameControllerGetAttached(pad)) {
        pad = NULL;
        for (i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); if (pad) break; }
        if (!pad) { prev = 0; return; }
    }
    {
        /* The layout can be flipped from this very menu, mid-press: the
         * press that flips it starts under one mapping and ends under
         * the other. On a change, release every face key and swallow
         * the face buttons until they are all up, so no key sticks
         * down and the held press does not land again as its new
         * meaning (the confirm press becoming a cancel). */
        static int swap_was = -1, swallow;
        int swap = nv_window_pref_nintendo_layout
                && nv_window_pref_nintendo_layout();
        if (swap != swap_was) {
            if (swap_was >= 0) {
                io.AddKeyEvent(ImGuiKey_GamepadFaceDown,  false);
                io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
                io.AddKeyEvent(ImGuiKey_GamepadFaceLeft,  false);
                io.AddKeyEvent(ImGuiKey_GamepadFaceUp,    false);
                prev = 0;
                swallow = 1;
            }
            swap_was = swap;
        }
        {
            int rawface = 0;
            for (i = 0; i < (int)(sizeof map / sizeof map[0]); i++) {
                ImGuiKey k = map[i].k;
                int down = SDL_GameControllerGetButton(pad, map[i].b) ? 1 : 0;
                if (k == ImGuiKey_GamepadFaceDown || k == ImGuiKey_GamepadFaceRight
                 || k == ImGuiKey_GamepadFaceLeft || k == ImGuiKey_GamepadFaceUp) {
                    if (down) rawface = 1;
                    if (swallow) down = 0;
                    if (swap) {   /* the pairs trade keys, as for the game */
                        if      (k == ImGuiKey_GamepadFaceDown)  k = ImGuiKey_GamepadFaceRight;
                        else if (k == ImGuiKey_GamepadFaceRight) k = ImGuiKey_GamepadFaceDown;
                        else if (k == ImGuiKey_GamepadFaceLeft)  k = ImGuiKey_GamepadFaceUp;
                        else if (k == ImGuiKey_GamepadFaceUp)    k = ImGuiKey_GamepadFaceLeft;
                    }
                }
                if (down) cur |= 1u << i;
                if ((cur ^ prev) & (1u << i)) io.AddKeyEvent(k, down != 0);
            }
            if (swallow && !rawface) swallow = 0;
        }
    }
    prev = cur;
    {
        static const struct { SDL_GameControllerAxis a; ImGuiKey neg, pos; } ax[] = {
            { SDL_CONTROLLER_AXIS_LEFTX, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight },
            { SDL_CONTROLLER_AXIS_LEFTY, ImGuiKey_GamepadLStickUp,   ImGuiKey_GamepadLStickDown },
        };
        for (i = 0; i < 2; i++) {
            float v = SDL_GameControllerGetAxis(pad, ax[i].a) / 32767.0f;
            float n = v < -0.25f ? -v : 0.0f, p = v > 0.25f ? v : 0.0f;
            io.AddKeyAnalogEvent(ax[i].neg, n > 0.0f, n);
            io.AddKeyAnalogEvent(ax[i].pos, p > 0.0f, p);
        }
    }
}
#endif /* __SWITCH__ */

extern "C" void xbox_MenuFrame(void)
{
    if (!g_ready || (!g_open && !g_busy_total)) return;
    SDL_LockMutex(g_mu);
    /* Full screen hides the pointer (nv2a_gl.c), and the menu can switch it:
     * ImGui draws its own then, which shows only once a mouse moves. */
    ImGui::GetIO().MouseDrawCursor = g_open && SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE;
    ImGui_ImplSDL2_NewFrame();
#if defined(__SWITCH__)
    if (g_open) switch_feed_nav();
#endif
    scale_to_window();
    ImGui::NewFrame();
    if (g_open) menu_build(g_just_opened);
    g_just_opened = 0;
    if (g_busy_total) menu_build_busy(g_busy_what, g_busy_done, g_busy_total);
    ImGui::Render();
    SDL_UnlockMutex(g_mu);
}

extern "C" void xbox_MenuDraw(void)
{
    ImDrawData *dd;
    if (!g_ready || (!g_open && !g_busy_total)) return;
    SDL_LockMutex(g_mu);
    dd = ImGui::GetDrawData();
    if (dd) {
        /* The renderer's GL objects are made (the first time) and used here,
         * in the context of the thread that swaps: the GL thread never
         * touches them, so nothing crosses between the two contexts. */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplOpenGL3_RenderDrawData(dd);
    }
    SDL_UnlockMutex(g_mu);
}

extern "C" void xbox_MenuBusy(const char *what, int done, int total)
{
    g_busy_what = what;
    g_busy_done = done;
    g_busy_total = total;
}

extern "C" int xbox_MenuIsOpen(void)
{
    return g_ready ? g_open : -1;
}
