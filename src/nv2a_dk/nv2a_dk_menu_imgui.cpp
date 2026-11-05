// The settings menu's front-end: Dear ImGui over the deko3d
// backend in nv2a_dk_imgui_dk.cpp, styled to the approved mock-2
// tokens (the menu core's palette: charcoal panel, orange active
// tab and selection, cyan and magenta accents, amber values, flat
// solid colours throughout).
//
// Over that base, the game-assets skin (nv2a_dk_menu_skin.cpp)
// draws JSRF's own art where it serves -- the badge, the game's
// UI font for heads and labels, the panel wash, the spray-can
// cursor, the button tiles -- all of it loaded at runtime from
// the player's own game files. Every skin call site is gated on
// dk_menu_skin_ok(); when the art cannot load, what renders is
// exactly the pre-skin menu described above.
//
// This replaces the hand-drawn menu (rectangles, baked font atlas,
// premultiplied 2D blits) wholesale: that path's image machinery is
// what froze the game on menu open (RUN53-CRASH-AUDIT and
// GLYPH-PATH-AUDIT in the project notes), while everything the rows
// DO was proven sound and stays in the core (nv2a_dk_menu.c), which
// this file only renders and pokes through nv2a_dk_menu_state.h.
//
// Input: the pad bitmask the input thread latches in the core
// (DK_MENU_* bits) is turned into ImGui key events here, on the
// renderer thread, and ImGui's gamepad navigation drives the
// widgets. State changes therefore happen on the renderer thread
// now; the settings stores are plain files and the engine requests
// are the same cross-thread latches the core always used.

#include <cstdio>
#include <cstring>
#include <ctime>

#include <switch.h>

#include "imgui.h"
#include "imgui_internal.h"  // SetFocusID: the nav focus restore (see note_focus)
#include "nv2a_dk_imgui_dk.h"
#include "nv2a_dk_menu.h"
#include "nv2a_dk_menu_skin.h"
#include "nv2a_dk_menu_state.h"

#if defined(XBOXRECOMP_DK_MENU)

namespace {

/* The mock-2 palette (the core's COL_* values, as ImGui colours). */
constexpr ImVec4 kPanelBase{0.051f, 0.078f, 0.094f, 0.96f};  // #0D1418, dense over the game
constexpr ImVec4 kPanelTeal{0.000f, 0.302f, 0.329f, 0.96f};  // #004D54 at kPanelBase's alpha:
// the game's own 2D menus sit on this dark teal field (sampled from the VS
// screens in Garage01 -- see the game-assets survey); skin mode only, below.
constexpr ImVec4 kPanelEdge{0.360f, 0.400f, 0.450f, 1.0f};
constexpr ImVec4 kRowsFace{0.039f, 0.067f, 0.086f, 0.78f};   // #0A1116, a layer over the panel
constexpr ImVec4 kRowsEdge{0.192f, 0.259f, 0.302f, 1.0f};   // #31424D
constexpr ImVec4 kHelpFace{0.043f, 0.102f, 0.125f, 0.70f};   // #0B1A20, a lighter layer again
constexpr ImVec4 kModalFace{0.075f, 0.102f, 0.125f, 0.98f};  // #131A20
constexpr ImVec4 kCyan{0.184f, 0.831f, 0.910f, 1.0f};       // #2FD4E8
constexpr ImVec4 kOrange{0.894f, 0.341f, 0.180f, 1.0f};     // #E4572E
constexpr ImVec4 kMagenta{0.914f, 0.227f, 0.549f, 1.0f};    // #E93A8C
constexpr ImVec4 kAmber{1.000f, 0.808f, 0.420f, 1.0f};      // #FFCE6B
constexpr ImVec4 kText{0.929f, 0.945f, 0.953f, 1.0f};       // #EDF1F3
constexpr ImVec4 kTitle{0.957f, 0.969f, 0.973f, 1.0f};      // #F4F7F8
constexpr ImVec4 kHelpBody{0.765f, 0.808f, 0.831f, 1.0f};   // #C3CED4
constexpr ImVec4 kHelpName{1.000f, 0.620f, 0.369f, 1.0f};   // #FF9E5E
constexpr ImVec4 kMuted{0.576f, 0.639f, 0.678f, 1.0f};      // #93A3AD
constexpr ImVec4 kFoot{0.541f, 0.592f, 0.639f, 1.0f};       // #8A97A3
/* Skin-mode lifts of the two quiet greys (build H). On the shipped
 * field the originals are fine, but on the retint's teal
 * (kPanelTeal) kFoot measures ~3.2:1 and kMuted ~3.7:1 -- the
 * footer words and the header subtitle go muddy. The lifts land
 * at ~4.6:1 and ~4.8:1 on the teal. Skin-only use sites: the
 * fallback keeps the shipped values untouched. */
constexpr ImVec4 kFootBright{0.660f, 0.710f, 0.750f, 1.0f};   // #A8B5BF, on teal
constexpr ImVec4 kMutedBright{0.680f, 0.730f, 0.770f, 1.0f};  // #ADBABC, on teal
constexpr ImVec4 kTabText{0.565f, 0.624f, 0.667f, 1.0f};    // #909FAA
constexpr ImVec4 kTabFace{0.078f, 0.122f, 0.153f, 1.0f};    // #141F27
constexpr ImVec4 kInk{0.078f, 0.063f, 0.047f, 1.0f};        // #14100C
constexpr ImVec4 kSelBand{0.043f, 0.239f, 0.278f, 1.0f};    // #0B3D47
constexpr ImVec4 kSelShadow{0.016f, 0.027f, 0.039f, 1.0f};  // #04070A
constexpr ImVec4 kTxtShadow{0.020f, 0.031f, 0.039f, 1.0f};  // #05080A
constexpr ImVec4 kRule{0.220f, 0.094f, 0.165f, 1.0f};       // #38182A
constexpr ImVec4 kChipFace{0.024f, 0.039f, 0.047f, 1.0f};   // #060A0C
constexpr ImVec4 kChipEdge{0.275f, 0.329f, 0.369f, 1.0f};   // #46545E
constexpr ImVec4 kBoxFace{0.039f, 0.059f, 0.071f, 1.0f};    // #0A0F12
constexpr ImVec4 kOffWord{0.471f, 0.514f, 0.553f, 1.0f};    // #78838D
constexpr ImVec4 kDisabled{0.440f, 0.488f, 0.529f, 1.0f};   // #707C87
constexpr ImVec4 kDisEdge{0.220f, 0.259f, 0.294f, 1.0f};    // #38424B
constexpr ImVec4 kActFace{0.114f, 0.157f, 0.184f, 1.0f};    // #1D282F
constexpr ImVec4 kActEdge{0.333f, 0.388f, 0.427f, 1.0f};    // #55636D
constexpr ImVec4 kActDis{0.098f, 0.125f, 0.145f, 1.0f};     // #192025
constexpr ImVec4 kKeyFace{0.110f, 0.149f, 0.176f, 1.0f};    // #1C262D
constexpr ImVec4 kKeyText{0.843f, 0.871f, 0.890f, 1.0f};    // #D7DEE3
constexpr ImVec4 kHazardInk{0.043f, 0.055f, 0.063f, 1.0f};  // #0B0E11
constexpr ImVec4 kWhite{0.980f, 0.990f, 1.000f, 1.0f};
/* State tints on the sister ports' pattern: the accent colour at
 * partial strength over the panel (Dusklight's interactive states
 * are its accent at 20%/40%; SwitchWakerHD's border is its teal at
 * 45%). */
constexpr ImVec4 kBorderGlow{0.184f, 0.831f, 0.910f, 0.35f};   // our cyan, as WakerHD's teal border
constexpr ImVec4 kBtnHover{0.304f, 0.157f, 0.120f, 1.0f};     // kOrange at 30% over kPanelBase
constexpr ImVec4 kCyanDim{0.184f, 0.831f, 0.910f, 0.55f};
constexpr ImVec4 kScrollBg{0.051f, 0.078f, 0.094f, 0.35f};
constexpr ImVec4 kRuleSoft{0.192f, 0.259f, 0.302f, 0.55f};  // kRowsEdge, softened: the footer's hairline

ImU32 u32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

bool g_init = false, g_broken = false;
uint32_t g_prev_buttons = 0;
double g_last_t = 0.0;
int g_page_seen = -1;    // the page the built UI last showed
int g_focus_row = -1;    // last frame's focused row: the band and the help pane
int g_restore_row = -1;  // a focus restore is pending for this row
int g_restore_age = 0;   // frames the pending restore has gone unmet
int g_first_enabled = -1;  // this frame's first enabled row (restore fallback)
bool g_reopen = false;   // the frame stream resumed after a gap: the menu was shut
int g_choice_row = -1;   // the row whose choice popup is open or opening
bool g_choice_fresh = false;
ImVec2 g_choice_anchor;
float g_choice_w = 0.0f;
int g_modal_shown = DK_ACT_NONE;
bool g_modal_fresh = false;
float g_hold_l = 0.0f, g_hold_r = 0.0f;  // slider keys' hold time, for the step size

/* SwitchWakerHD's font recipe (runtime/src/platform/
 * overlay_switch.cpp, load_font): the console's own shared font,
 * kept by the system, so the atlas does not own it. */
bool load_font(ImGuiIO &io)
{
    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        PlFontData font;
        if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            if (io.Fonts->AddFontFromMemoryTTF(font.address, int(font.size), 17.0f, &cfg)) {
                fprintf(stderr, "[MENU] imgui font: the system's shared font\n");
                return true;
            }
        }
    }
    return false;
}

void setup_fonts(ImGuiIO &io)
{
    if (load_font(io)) return;
    /* Their fallback: ImGui's own vector font at the same size. */
    ImFontConfig cfg;
    cfg.SizePixels = 17.0f;
    io.Fonts->AddFontDefaultVector(&cfg);
    fprintf(stderr, "[MENU] imgui font: the shared font is unavailable; ImGui's default\n");
}

void setup_style()
{
    ImGuiStyle &st = ImGui::GetStyle();
    /* The sister ports' metrics (WORKSTREAM 2; sources cited in
     * IMGUI-FIX-POLISH.md): SwitchWakerHD's rounding set and
     * padding (runtime/src/overlay/overlay.cpp, setup_style) on
     * Dusklight's spacing grid (res/rml/theme.rcss: 4/8/12/16/24),
     * with mock-2's palette carried into their alpha-layered state
     * language instead of the old all-square, all-opaque look. */
    st.WindowRounding = 10.0f;
    st.ChildRounding = 6.0f;
    st.FrameRounding = 5.0f;
    st.PopupRounding = 6.0f;
    st.ScrollbarRounding = 6.0f;
    st.GrabRounding = 5.0f;
    st.TabRounding = 6.0f;
    st.WindowBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 1.0f;  // WakerHD: keeps unchecked boxes visible on the dark ground
    st.PopupBorderSize = 1.0f;
    st.WindowPadding = ImVec2(20.0f, 16.0f);
    st.FramePadding = ImVec2(10.0f, 6.0f);
    st.ItemSpacing = ImVec2(12.0f, 8.0f);
    st.ItemInnerSpacing = ImVec2(8.0f, 4.0f);
    st.IndentSpacing = 16.0f;
    st.ScrollbarSize = 12.0f;
    st.DisabledAlpha = 0.55f;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDisabled;
    c[ImGuiCol_WindowBg] = kPanelBase;
    c[ImGuiCol_PopupBg] = kModalFace;
    c[ImGuiCol_Border] = kBorderGlow;
    c[ImGuiCol_FrameBg] = kBoxFace;
    c[ImGuiCol_FrameBgHovered] = kActFace;
    c[ImGuiCol_FrameBgActive] = kActFace;
    c[ImGuiCol_CheckMark] = kCyan;
    c[ImGuiCol_Button] = kActFace;
    c[ImGuiCol_ButtonHovered] = kBtnHover;
    c[ImGuiCol_ButtonActive] = kOrange;
    c[ImGuiCol_Header] = kSelBand;
    c[ImGuiCol_HeaderHovered] = kBtnHover;
    c[ImGuiCol_HeaderActive] = kSelBand;
    c[ImGuiCol_Separator] = kRowsEdge;
    c[ImGuiCol_ScrollbarBg] = kScrollBg;
    c[ImGuiCol_ScrollbarGrab] = kRowsEdge;
    c[ImGuiCol_ScrollbarGrabHovered] = kCyanDim;
    c[ImGuiCol_ScrollbarGrabActive] = kCyan;
    c[ImGuiCol_NavCursor] = kAmber;  // WakerHD's gold controller cursor, in our amber
    c[ImGuiCol_TextSelectedBg] = kSelBand;
}

bool init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nothing written next to the game
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "jsrf_deko3d";
    io.ConfigNavCaptureKeyboard = true;
    io.KeyRepeatDelay = 0.275f;  // ImGui's own tuning -- and so the
    io.KeyRepeatRate = 0.050f;   // GL menu's: it never overrode these
    setup_style();
    setup_fonts(io);
    ImGui::GetStyle().FontSizeBase = 17.0f;
    if (!dk_imgui_backend_init()) {
        g_broken = true;
        return false;
    }
    /* The game-assets skin (the game's own art, loaded from the
     * player's copy): optional by construction -- if it cannot
     * load, the menu below renders exactly as it always has. */
    dk_menu_skin_init();
    /* Skin live: the window field takes the game's dark teal
     * (kPanelTeal) in place of the shipped blue-black. Skin off:
     * the style is never touched, so the menu renders exactly
     * as shipped. The pane faces stay layered over it as before. */
    if (dk_menu_skin_ok())
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = kPanelTeal;
    return true;
}

/* The latched pad bitmask as ImGui key events. ImGui's gamepad
 * slots are named for an Xbox pad: FaceDown is its Activate,
 * FaceRight its Cancel. The menu's contract is the physical pad
 * (xinput_device.c): the right face button (printed A) selects, the
 * bottom one (printed B) goes back -- so the bits are fed to the
 * slots by function, not by position, the same remap
 * SwitchWakerHD's gamepad_nav.h documents for its pad. */
void feed_input(ImGuiIO &io, uint32_t buttons)
{
    static const struct { uint32_t bit; ImGuiKey key; } kMap[] = {
        { DK_MENU_UP, ImGuiKey_GamepadDpadUp },
        { DK_MENU_DOWN, ImGuiKey_GamepadDpadDown },
        { DK_MENU_LEFT, ImGuiKey_GamepadDpadLeft },
        { DK_MENU_RIGHT, ImGuiKey_GamepadDpadRight },
        { DK_MENU_SELECT, ImGuiKey_GamepadFaceDown },
        { DK_MENU_CANCEL, ImGuiKey_GamepadFaceRight },
        { DK_MENU_TAB_LEFT, ImGuiKey_GamepadL1 },
        { DK_MENU_TAB_RIGHT, ImGuiKey_GamepadR1 },
    };
    uint32_t changed = buttons ^ g_prev_buttons;
    for (const auto &m : kMap)
        if (changed & m.bit)
            io.AddKeyEvent(m.key, (buttons & m.bit) != 0);
    g_prev_buttons = buttons;
}

/* A plate that is pure decoration (the footer keys, HELP):
 * a styled button the pad can never land on. */
void plate(const char *label, ImVec2 size, ImVec4 bg, ImVec4 fg)
{
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::Button(label, size);
    ImGui::PopItemFlag();
    ImGui::PopStyleColor(4);
}

void build_menu(int fb_w, int fb_h)
{
    if (g_reopen) {  // the menu was shut and opened again: re-arm the focus restore
        g_reopen = false;
        g_page_seen = -1;
    }
    int npages = 0;
    const DkPage *pages = dk_menu_pages(&npages);
    const float u = fb_h >= 1000 ? 1.5f : 1.0f;  // the UI's scale step, as the old menu's sc
    ImDrawList *wdl = nullptr;

    /* Pages move on the bumpers, as they always have -- when no
     * modal is up and no choice list is open to eat the press. */
    int page = dk_menu_page();
    if (dk_menu_modal_action() == DK_ACT_NONE && g_choice_row < 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1))
            dk_menu_set_page(page - 1);
        else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1))
            dk_menu_set_page(page + 1);
        page = dk_menu_page();
    }
    if (page != g_page_seen) {
        g_page_seen = page;
        g_choice_row = -1;
        g_focus_row = dk_menu_focus_row();
        g_restore_row = g_focus_row;
        g_restore_age = 0;
    }
    const DkPage &pg = pages[page];
    const DkRow *rows = pg.rows;

    const float pw = fb_w * 0.86f, ph = fb_h * 0.86f;
    ImGui::SetNextWindowPos(ImVec2((fb_w - pw) * 0.5f, (fb_h - ph) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
    if (!ImGui::Begin("##jsrf_menu", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    ImGui::PushFont(nullptr, 17.0f * u);
    wdl = ImGui::GetWindowDrawList();

    /* ── Header: the title and SETTINGS ── */
    {
        float y0 = ImGui::GetCursorPosY();
        if (dk_menu_skin_ok()) {
            /* The title in the game's own font, with room at the
             * left for the badge sticker (drawn over the corner
             * once the tabs are in, below). SETTINGS is right-
             * aligned by its game-font width, as the text it
             * replaces was by its own. Layout height is unchanged:
             * the same 34-unit line the text title took. */
            ImVec2 hp = ImGui::GetCursorScreenPos();
            float settings_x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x
                             - dk_menu_game_text_width("SETTINGS", 13.0f * u);
            dk_menu_draw_game_text(wdl, ImVec2(ImGui::GetWindowPos().x + settings_x, hp.y + 9.0f * u),
                                   "SETTINGS", 13.0f * u, u32(kMutedBright));
            ImGui::PushFont(nullptr, 34.0f * u);
            float th = ImGui::GetTextLineHeight();
            ImGui::PopFont();
            float ty = hp.y + (th - 27.0f * u) * 0.5f;
            float w1 = dk_menu_draw_game_text(wdl, ImVec2(hp.x + 104.0f * u, ty),
                                              "JSRF ", 27.0f * u, u32(kTitle));
            dk_menu_draw_game_text(wdl, ImVec2(hp.x + 104.0f * u + w1 + 2.0f * u, ty),
                                   "UNLEASHED NX", 27.0f * u, u32(kOrange));
            ImGui::SetCursorPosY(y0 + th + 6.0f * u);
        } else {
            float settings_x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x
                             - ImGui::CalcTextSize("SETTINGS").x;
            ImGui::SetCursorPos(ImVec2(settings_x, y0 + 8.0f * u));
            ImGui::TextColored(kMuted, "SETTINGS");
            ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, y0));
            ImGui::PushFont(nullptr, 34.0f * u);
            ImGui::TextColored(kTitle, "JSRF ");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(kOrange, "UNLEASHED NX");
            float th = ImGui::GetTextLineHeight();
            ImGui::PopFont();
            ImGui::SetCursorPosY(y0 + th + 6.0f * u);
        }
    }
    /* ── The spray stripe: three bands, stepped ends ── */
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        /* Skin bands run a unit taller (build H): the mock's
         * stripe is chunkier than the shipped 3u bands, which read
         * thin under the title. The fallback keeps its 3u. */
        float w = ImGui::GetContentRegionAvail().x,
              sh = (dk_menu_skin_ok() ? 4.0f : 3.0f) * u;
        if (dk_menu_skin_ok()) {
            /* The game's TV-static texture, tinted per band, in
             * the same stepped layout as the flat bands. */
            ImVec2 uv0, uv1;
            dk_menu_skin_sprite_uv(DK_SPR_STATIC, uv0, uv1);
            ImTextureRef at = dk_menu_skin_atlas_ref();
            wdl->AddImage(at, p, ImVec2(p.x + w, p.y + sh), uv0, uv1, u32(kCyan));
            wdl->AddImage(at, ImVec2(p.x, p.y + sh), ImVec2(p.x + w * 0.93f, p.y + 2.0f * sh), uv0, uv1, u32(kOrange));
            wdl->AddImage(at, ImVec2(p.x, p.y + 2.0f * sh), ImVec2(p.x + w * 0.86f, p.y + 3.0f * sh), uv0, uv1, u32(kMagenta));
        } else {
            wdl->AddRectFilled(p, ImVec2(p.x + w, p.y + sh), u32(kCyan));
            wdl->AddRectFilled(ImVec2(p.x, p.y + sh), ImVec2(p.x + w * 0.93f, p.y + 2.0f * sh), u32(kOrange));
            wdl->AddRectFilled(ImVec2(p.x, p.y + 2.0f * sh), ImVec2(p.x + w * 0.86f, p.y + 3.0f * sh), u32(kMagenta));
        }
        ImGui::Dummy(ImVec2(w, 3.0f * sh + 4.0f * u));
    }
    /* ── The tabs: colour-block plates, display only -- the
     * bumpers move pages, so the pad's focus never leaves the
     * rows, exactly as the hand-drawn menu had it. ── */
    {
        /* Left-packed with one even gap. The row used to spread
         * the tabs across the full width by dividing the leftover
         * space between them (space-between), which read as
         * scruffy next to the left-packed rows below and made the
         * gaps depend on the page names' widths; the plates now
         * sit shoulder to shoulder like the rest of the layout.
         * The active tab's orange plate + marker are unchanged. */
        float pad = 12.0f * u, tab_h = 32.0f * u;
        float widths[16];
        bool gtext[16];
        const bool skin = dk_menu_skin_ok();
        for (int i = 0; i < npages && i < 16; i++) {
            /* A page name the game font covers is drawn in it;
             * any other keeps the plate's own text (none today --
             * every page name is plain capitals). */
            gtext[i] = skin && dk_menu_game_text_covered(pages[i].name);
            widths[i] = (gtext[i] ? dk_menu_game_text_width(pages[i].name, 14.0f * u)
                                  : ImGui::CalcTextSize(pages[i].name).x)
                      + 2.0f * pad + (i == page ? 14.0f * u : 0.0f);
        }
        const float gap = 10.0f * u;
        for (int i = 0; i < npages && i < 16; i++) {
            if (i) ImGui::SameLine(0.0f, gap);
            bool on = i == page;
            /* A game-font tab's plate carries an invisible ID
             * ("##NAME": no display text, a unique ID), so the
             * plates never share one widget ID. */
            char tab_id[64];
            snprintf(tab_id, sizeof tab_id, "##%s", pages[i].name);
            plate(gtext[i] ? tab_id : pages[i].name, ImVec2(widths[i], tab_h),
                  on ? kOrange : kTabFace, on ? kInk : kTabText);
            ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
            if (skin) {
                /* The message-panel's borderless interior as a
                 * wash over the plate (the bordered panel itself
                 * is 46px of art on a 30px plate; its border is
                 * reserved for the big blocks below). */
                ImVec2 uv0, uv1;
                dk_menu_skin_sprite_uv(DK_SPR_PANEL_IN, uv0, uv1);
                wdl->AddImage(dk_menu_skin_atlas_ref(),
                              ImVec2(rmin.x + 1.0f, rmin.y + 1.0f), ImVec2(rmax.x - 1.0f, rmax.y - 1.0f),
                              uv0, uv1,
                              on ? u32(ImVec4(1.0f, 0.45f, 0.20f, 0.50f))
                                 : u32(ImVec4(1.0f, 1.0f, 1.0f, 0.30f)));
            }
            if (on) {
                if (!skin) {
                    /* The shipped active-tab accents: the cyan top
                     * strip and the triangle. With the skin live
                     * both are gone (Brandon, Nov 5): the spray can
                     * below is the sole selection indicator, with
                     * no accent line beside it. */
                    wdl->AddRectFilled(rmin, ImVec2(rmax.x, rmin.y + 2.0f * u), u32(kCyan));
                    ImVec2 c(rmin.x + 8.0f * u, (rmin.y + rmax.y) * 0.5f);
                    wdl->AddTriangleFilled(ImVec2(c.x, c.y - 5.0f * u), ImVec2(c.x, c.y + 5.0f * u),
                                           ImVec2(c.x + 6.0f * u, c.y), u32(kInk));
                } else {
                    /* The can at the active plate's left edge,
                     * straddling it -- the same placement as the
                     * row cursor, so one glyph marks selection in
                     * both strips. Drawn on the window list: no
                     * child clip can cut it. */
                    ImVec2 uv0, uv1;
                    dk_menu_skin_sprite_uv(DK_SPR_CAN, uv0, uv1);
                    ImVec2 csz = dk_menu_skin_sprite_px(DK_SPR_CAN);
                    float ch = 40.0f * u, cw = ch * csz.x / csz.y;
                    float cy = (rmin.y + rmax.y) * 0.5f;
                    wdl->AddImage(dk_menu_skin_atlas_ref(),
                                  ImVec2(rmin.x - cw * 0.5f, cy - ch * 0.5f),
                                  ImVec2(rmin.x + cw * 0.5f, cy + ch * 0.5f), uv0, uv1);
                }
            }
            if (gtext[i]) {
                float tw = dk_menu_game_text_width(pages[i].name, 14.0f * u);
                float cx = (rmin.x + rmax.x) * 0.5f + (on && !skin ? 7.0f * u : 0.0f);
                dk_menu_draw_game_text(wdl, ImVec2(cx - tw * 0.5f, (rmin.y + rmax.y) * 0.5f - 7.0f * u),
                                       pages[i].name, 14.0f * u, u32(on ? kInk : kTabText));
            }
        }
    }
    /* The badge sticker: drawn last in the header stack, so it
     * sits over the panel corner and the spray band the way the
     * approved mock has it. Its size is the mock's (96x78 at the
     * 720p scale step). */
    if (dk_menu_skin_ok()) {
        ImVec2 uv0, uv1;
        dk_menu_skin_sprite_uv(DK_SPR_BADGE, uv0, uv1);
        ImVec2 wp = ImGui::GetWindowPos();
        wdl->AddImage(dk_menu_skin_atlas_ref(),
                      ImVec2(wp.x + 6.0f * u, wp.y + 3.0f * u),
                      ImVec2(wp.x + 102.0f * u, wp.y + 81.0f * u), uv0, uv1);
    }

    /* ── The body: rows and help on their own sub-panels ── */
    const float footer_h = 38.0f * u;
    const float body_h = ImGui::GetContentRegionAvail().y - footer_h - 6.0f * u;
    const float rows_w = ImGui::GetContentRegionAvail().x * 0.60f;
    /* A core modal (map jump confirm, cheat warning, ...) is armed.
     * Sampled here, after page handling: landing on the Cheats page
     * arms the warning inside set_page, and a choice pick arms its
     * confirm during last frame's popup block -- both must count
     * from this frame's first row on. While a modal is up the rows
     * still draw, but the focus machinery below stands down (see
     * note_focus): the dialog owns the pad. */
    const bool modal_up = dk_menu_modal_action() != DK_ACT_NONE;
    /* Shared engine state, sampled once a frame at most (the map
     * rows share one availability query, the teleport rows one
     * ready query), never once per row. */
    int snap_map_avail = -1, snap_tp_ready = -1;
    int focused_now = -1;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kRowsFace);
    ImGui::PushStyleColor(ImGuiCol_Border, kRowsEdge);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * u, 12.0f * u));
    ImGui::BeginChild("##rows", ImVec2(rows_w, body_h), ImGuiChildFlags_Borders);
    ImGui::PushFont(nullptr, 17.0f * u);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        /* The selection band for the focused row: a near-black
         * shadow offset, the deep-teal band, an orange chunk at its
         * head and a white triangle -- drawn under the row's own
         * widgets, from last frame's focus (the nav cursor itself
         * is instant). */
        /* When a row takes focus its text steps right to clear the
         * band's orange chunk and white marker; the step must reach
         * past the marker's right edge (the marker spans 7u..13u)
         * with a letter's worth of air -- at the old 8u the marker
         * struck through the first glyph of focused labels. */
        const float focus_dx = 16.0f * u;
        auto draw_band = [&](int i, float h) {
            if (i != g_focus_row) return;
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            dl->AddRectFilled(ImVec2(p.x + 2.0f * u, p.y + 2.0f * u),
                              ImVec2(p.x + w + 2.0f * u, p.y + h + 2.0f * u), u32(kSelShadow));
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(kSelBand));
            dl->AddRectFilled(p, ImVec2(p.x + 3.0f * u, p.y + h), u32(kOrange));
            /* Marker inset inside the band: starts 7u in (clear of
             * the 3u chunk and off the pane edge), tip at 13u --
             * inside focus_dx, so text never meets it. */
            float cy = p.y + h * 0.5f, tx = p.x + 7.0f * u;
            if (!dk_menu_skin_ok())
                dl->AddTriangleFilled(ImVec2(tx, cy - 5.0f * u), ImVec2(tx, cy + 5.0f * u),
                                      ImVec2(tx + 6.0f * u, cy), u32(kWhite));
            if (dk_menu_skin_ok()) {
                /* The game's spray can rides the band's left edge
                 * as the cursor, straddling the pane's border --
                 * the clip is pushed out to the can's own rect so
                 * the child window does not cut the overhang. */
                ImVec2 uv0, uv1;
                dk_menu_skin_sprite_uv(DK_SPR_CAN, uv0, uv1);
                ImVec2 csz = dk_menu_skin_sprite_px(DK_SPR_CAN);
                float ch = 44.0f * u, cw = ch * csz.x / csz.y;
                ImVec2 c0(p.x - 6.0f * u - cw * 0.5f, p.y + h * 0.5f - ch * 0.5f);
                ImVec2 c1(c0.x + cw, c0.y + ch);
                dl->PushClipRect(ImVec2(c0.x - 2.0f * u, c0.y - 2.0f * u),
                                 ImVec2(c1.x + 2.0f * u, c1.y + 2.0f * u), false);
                dl->AddImage(dk_menu_skin_atlas_ref(), c0, c1, uv0, uv1);
                dl->PopClipRect();
            }
        };
        auto note_focus = [&](int i, bool en) {
            if (en && g_first_enabled < 0) g_first_enabled = i;
            if (ImGui::IsItemFocused()) {
                focused_now = i;
                if (g_restore_row == i) {
                    g_restore_row = -1;
                    g_restore_age = 0;
                }
            } else if (g_restore_row == i && en && !modal_up) {
                /* Put the pad's focus on this row outright. The
                 * public SetItemDefaultFocus() cannot do it here:
                 * it only acts while the item's window is appearing
                 * AND a nav init is pending (imgui.cpp,
                 * SetItemDefaultFocus), and neither holds on a page
                 * switch, a popup close or a menu reopen in a window
                 * that has been open all along -- the pad was left
                 * with no focus at all in exactly those moments.
                 * SetFocusID is the call the nav system itself uses
                 * to restore focus; called on the row's own widget,
                 * in its own window, right after submission.
                 *
                 * Never while a modal is up: SetFocusID moves the
                 * nav window with no regard for the popup stack
                 * (imgui.cpp, SetFocusID -> SetNavWindow), so a
                 * restore armed by the pick that opened the modal
                 * would drag the pad's focus out of the dialog and
                 * park it on this row, behind it -- the dialog's
                 * buttons would never see a press, and the press
                 * would land on the row instead, reopening the
                 * choice list over (and closing) the modal. The
                 * restore stays armed and lands when the modal
                 * closes, which is where it belongs. */
                ImGuiID id = ImGui::GetItemID();
                if (id != 0) ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
            }
        };
        g_first_enabled = -1;
        /* Row IDs carry the page: without this, the same row index
         * on two pages hashes to the same widget ID, and after a
         * tab switch the nav ID pointed at a row that does not
         * exist on the new page -- focus was a zombie until the
         * ID happened to exist again. */
        ImGui::PushID(page);
        for (int i = 0; i < pg.nrows; i++) {
            const DkRow *r = &rows[i];
            ImGui::PushID(i);
            if (r->kind == DK_R_SECTION) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f * u));
                char head[160];
                snprintf(head, sizeof head, "%s", r->label);
                if (dk_menu_skin_ok() && dk_menu_game_text_covered(head)) {
                    /* The head in the game's font, without the
                     * shipped "//" prefix (build H): the slashes
                     * are a code-comment tell, and a game's menu
                     * does not comment on itself. The fallback
                     * below prints its own "//" and is untouched.
                     * Coverage is checked on the label alone, which
                     * can only widen it. The advance is the text
                     * path's line height, so the rules below land
                     * where they always land. */
                    ImGui::PushFont(nullptr, 20.0f * u);
                    float lh = ImGui::GetTextLineHeight();
                    ImGui::PopFont();
                    dk_menu_draw_game_text(dl, ImGui::GetCursorScreenPos(), head, 15.0f * u, u32(kMagenta));
                    ImGui::Dummy(ImVec2(0.0f, lh));
                } else {
                    ImGui::PushFont(nullptr, 20.0f * u);  // a step up the type scale: heads over rows
                    ImGui::TextColored(kCyan, "//");
                    ImGui::SameLine(0.0f, 6.0f * u);
                    ImGui::TextColored(kMagenta, "%s", r->label);
                    ImGui::PopFont();
                }
                ImVec2 p = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                dl->AddLine(p, ImVec2(p.x + w, p.y), u32(kRule), 2.0f * u);
                dl->AddLine(p, ImVec2(p.x + 64.0f * u, p.y), u32(kMagenta), 2.0f * u);
                ImGui::Dummy(ImVec2(0.0f, 6.0f * u));
                ImGui::PopID();
                continue;
            }
            const unsigned flags = dk_menu_row_flags(r);
            bool en = true;
            if (flags & DK_ROW_F_TPGATE) {
                if (snap_tp_ready < 0) snap_tp_ready = dk_menu_tp_ready();
                en = snap_tp_ready != 0;
            } else if (flags & DK_ROW_F_MAP) {
                if (snap_map_avail < 0) snap_map_avail = dk_menu_map_avail();
                en = snap_map_avail != 0;
            } else if (r->enabled) {
                en = r->enabled(r->arg) != 0;
            }
            if (r->kind == DK_R_INFO && !en) {
                /* An INFO row whose enabled() says no is not drawn
                 * at all: it is a warning that exists only while its
                 * condition does (the File page's legacy-file row).
                 * Every other row kind keeps the standing shape --
                 * drawn, dimmed, and unfocusable while disabled.
                 * No INFO row carried an enabled() before this, so
                 * no existing page changes. */
                ImGui::PopID();
                continue;
            }
            if (!en) ImGui::BeginDisabled();
            const float fh = ImGui::GetFrameHeight();
            switch (r->kind) {
            case DK_R_CHECK: {
                draw_band(i, fh);
                if (i == g_focus_row) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + focus_dx);
                ImGui::TextUnformatted(r->label);
                bool v = r->get(r->arg) != 0;
                const char *word = v ? "ON" : "OFF";
                float word_w = ImGui::CalcTextSize(word).x;
                float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                /* Skin rows pull the word + box off the pane's
                 * edge to the same right margin the chips and
                 * pills now keep (build H); the shipped pair sits
                 * ~2u from the border. */
                if (dk_menu_skin_ok()) right -= 4.0f * u;
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(right - word_w - fh - 8.0f * u);
                ImGui::TextColored(v ? kCyan : kOffWord, "%s", word);
                ImGui::SameLine(0.0f, 6.0f * u);
                if (ImGui::Checkbox("##box", &v)) r->set(r->arg, v ? 1 : 0);
                note_focus(i, en);
                if (ImGui::IsItemFocused()) {
                    /* Directed set, guarded as the old input had it:
                     * a toggle-style setter is only called when the
                     * value would actually change. */
                    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) && r->get(r->arg))
                        r->set(r->arg, 0);
                    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) && !r->get(r->arg))
                        r->set(r->arg, 1);
                }
                break;
            }
            case DK_R_CHOICE: {
                draw_band(i, fh);
                ImVec2 rp = ImGui::GetCursorScreenPos();
                float rw = ImGui::GetContentRegionAvail().x;
                /* EnableNav: in this ImGui, InvisibleButton is
                 * NoNav unless asked (imgui_widgets.cpp) -- without
                 * it the row can neither take the pad's focus nor be
                 * passed over, a wall across the page. */
                bool activated = ImGui::InvisibleButton("##row", ImVec2(rw, fh),
                                                        ImGuiButtonFlags_EnableNav);
                note_focus(i, en);
                int nc = dk_menu_row_nchoices(r);
                int cur = r->get(r->arg);
                const char *val = cur >= 0 && cur < nc ? r->choices[cur] : "";
                float vw = ImGui::CalcTextSize(val).x;
                float chw = vw + (dk_menu_skin_ok() ? 16.0f : 20.0f) * u, chh = fh - 2.0f * u;
                /* Skin rows give the chip a right margin (build
                 * H): flush to the pane's edge, the widest value
                 * in the menu (MUSIC's USE AREA MIX) rode into
                 * the pane's border with no margin at all. The
                 * label is clipped to end clear of the chip, so
                 * a long label can never run under it. */
                float rin = dk_menu_skin_ok() ? 6.0f * u : 0.0f;
                ImVec2 c0(rp.x + rw - rin - chw, rp.y + 1.0f * u), c1(rp.x + rw - rin, rp.y + 1.0f * u + chh);
                float tx = rp.x + (i == g_focus_row ? focus_dx : 0.0f);
                if (dk_menu_skin_ok())
                    dl->PushClipRect(ImVec2(rp.x, rp.y), ImVec2(c0.x - 8.0f * u, rp.y + fh), true);
                dl->AddText(ImVec2(tx, rp.y + (fh - ImGui::GetTextLineHeight()) * 0.5f),
                            u32(en ? kText : kDisabled), r->label);
                if (dk_menu_skin_ok())
                    dl->PopClipRect();
                dl->AddRectFilled(c0, c1, u32(kChipFace), 3.0f * u);
                dl->AddRect(c0, c1, u32(en ? kChipEdge : kDisEdge), 3.0f * u, 1.0f, 0);
                dl->AddText(ImVec2(c0.x + 8.0f * u, c0.y + (chh - ImGui::GetTextLineHeight()) * 0.5f),
                            u32(en ? kAmber : kDisabled), val);
                float tri_x = c0.x + 11.0f * u + vw, tri_cy = (c0.y + c1.y) * 0.5f;
                if (!dk_menu_skin_ok())
                    dl->AddTriangleFilled(ImVec2(tri_x, tri_cy - 4.0f * u), ImVec2(tri_x, tri_cy + 4.0f * u),
                                          ImVec2(tri_x + 5.0f * u, tri_cy), u32(en ? kOrange : kDisabled));
                if (activated && en) {
                    g_choice_row = i;
                    g_choice_fresh = true;
                    g_choice_anchor = ImVec2(rp.x + rw, rp.y + fh);
                    g_choice_w = 0.0f;
                    for (int j = 0; j < nc; j++) {
                        float cw2 = ImGui::CalcTextSize(r->choices[j]).x;
                        if (cw2 > g_choice_w) g_choice_w = cw2;
                    }
                    g_choice_w += 28.0f * u;
                }
                if (ImGui::IsItemFocused()) {
                    if (flags & DK_ROW_F_MUSIC) {
                        /* Music picks are staged, not committed. */
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true))
                            dk_menu_music_stage_step(r, -1);
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true))
                            dk_menu_music_stage_step(r, +1);
                    } else if (flags & DK_ROW_F_TRACK) {
                        /* Track picks likewise: staged here, fired
                         * only when the list confirms them. */
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true))
                            dk_menu_track_stage_step(r, -1);
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true))
                            dk_menu_track_stage_step(r, +1);
                    } else if (flags & DK_ROW_F_CUSTOM) {
                        /* Custom picks likewise: staged here, fired
                         * only when the list confirms them. */
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true))
                            dk_menu_custom_stage_step(r, -1);
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true))
                            dk_menu_custom_stage_step(r, +1);
                    } else if (!(flags & DK_ROW_F_MAP) && nc > 0) {
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true))
                            r->set(r->arg, (cur + nc - 1) % nc);
                        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true))
                            r->set(r->arg, (cur + 1) % nc);
                    }
                }
                break;
            }
            case DK_R_BUTTON: {
                draw_band(i, fh);
                ImVec2 rp = ImGui::GetCursorScreenPos();
                float rw = ImGui::GetContentRegionAvail().x;
                bool activated = ImGui::InvisibleButton("##row", ImVec2(rw, fh),
                                                        ImGuiButtonFlags_EnableNav);
                note_focus(i, en);
                float plw = ImGui::CalcTextSize("RUN").x + 18.0f * u;
                /* The RUN pill takes the same skin right margin
                 * and label clip as the choice chip (build H). */
                float rin = dk_menu_skin_ok() ? 6.0f * u : 0.0f;
                ImVec2 c0(rp.x + rw - rin - plw, rp.y + 1.0f * u), c1(rp.x + rw - rin, rp.y + fh - 1.0f * u);
                float tx = rp.x + (i == g_focus_row ? focus_dx : 0.0f);
                if (dk_menu_skin_ok())
                    dl->PushClipRect(ImVec2(rp.x, rp.y), ImVec2(c0.x - 8.0f * u, rp.y + fh), true);
                dl->AddText(ImVec2(tx, rp.y + (fh - ImGui::GetTextLineHeight()) * 0.5f),
                            u32(en ? kText : kDisabled), r->label);
                if (dk_menu_skin_ok())
                    dl->PopClipRect();
                dl->AddRectFilled(c0, c1, u32(en ? kActFace : kActDis), 3.0f * u);
                dl->AddRect(c0, c1, u32(en ? kActEdge : kDisEdge), 3.0f * u, 1.0f, 0);
                float tri_cy = (c0.y + c1.y) * 0.5f;
                if (!dk_menu_skin_ok())
                    dl->AddTriangleFilled(ImVec2(c0.x + 5.0f * u, tri_cy - 4.0f * u),
                                          ImVec2(c0.x + 5.0f * u, tri_cy + 4.0f * u),
                                          ImVec2(c0.x + 10.0f * u, tri_cy), u32(en ? kOrange : kDisabled));
                float run_tx = dk_menu_skin_ok()
                    ? c0.x + ((c1.x - c0.x) - ImGui::CalcTextSize("RUN").x) * 0.5f
                    : c0.x + 13.0f * u;
                dl->AddText(ImVec2(run_tx, c0.y + ((c1.y - c0.y) - ImGui::GetTextLineHeight()) * 0.5f),
                            u32(en ? kAmber : kDisabled), "RUN");
                if (activated && en && r->run) r->run(r->arg);
                break;
            }
            case DK_R_INFO: {
                /* One item for the whole block: a tall Invisible-
                 * Button (the slider row's shape) whose height is
                 * the label line + the standard gap + the wrapped
                 * value, with both texts drawn inside it. The old
                 * shape -- a one-line Selectable for the label plus
                 * a free Text item for the value -- put three
                 * heights at war: the band was sized fh + val_h + 2u
                 * with no room for the label/value gap, so it
                 * stopped short of the value's last line; the
                 * Selectable painted its own hover background over
                 * the label line, splitting the block into two
                 * unrelated pieces; and the value was a separate
                 * item from its label entirely, so an unfocused
                 * block had no grouping at all. Here the band's
                 * height IS the item's height by construction, the
                 * button paints nothing, and label + value read as
                 * one row of the grid. */
                char buf[512];
                const char *v = r->value ? r->value(r->arg, buf, sizeof buf) : "";
                float rw = ImGui::GetContentRegionAvail().x;
                /* The wrap width is measured at the focused inset
                 * in both states, so the value wraps -- and the row
                 * heights itself -- identically whether or not the
                 * focus shift is applied: no re-flow when focus
                 * arrives on or leaves the row. */
                float wrap_w = rw - focus_dx - 6.0f * u;
                float val_h = ImGui::CalcTextSize(v, nullptr, false, wrap_w).y;
                float gap_y = ImGui::GetStyle().ItemSpacing.y;
                float total_h = fh + gap_y + val_h;
                draw_band(i, total_h);
                ImVec2 rp = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##row", ImVec2(rw, total_h),
                                       ImGuiButtonFlags_EnableNav);
                note_focus(i, en);
                if (dk_menu_skin_ok())
                    /* The big blocks get the message panel as a
                     * framed wash (the mock's STATUS treatment,
                     * applied to the INFO blocks as a class). */
                    dk_menu_draw_panel_wash(dl, rp, ImVec2(rp.x + rw, rp.y + total_h), 0.30f, 10.0f * u);
                float tx = rp.x + (i == g_focus_row ? focus_dx : 0.0f);
                dl->AddText(ImVec2(tx, rp.y + (fh - ImGui::GetTextLineHeight()) * 0.5f),
                            u32(en ? kText : kDisabled), r->label);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                            ImVec2(tx, rp.y + fh + gap_y),
                            u32(en ? kHelpBody : kDisabled), v, nullptr, wrap_w);
                break;
            }
            case DK_R_SLIDER: {
                float rh = fh * 2.0f + 4.0f * u;
                draw_band(i, rh);
                ImVec2 rp = ImGui::GetCursorScreenPos();
                float rw = ImGui::GetContentRegionAvail().x;
                ImGui::InvisibleButton("##row", ImVec2(rw, rh), ImGuiButtonFlags_EnableNav);
                note_focus(i, en);
                float v = r->getf(r->arg);
                char vbuf[32];
                snprintf(vbuf, sizeof vbuf, "%.0f", double(v));
                float tx = rp.x + (i == g_focus_row ? focus_dx : 0.0f);
                dl->AddText(ImVec2(tx, rp.y + 2.0f * u), u32(en ? kText : kDisabled), r->label);
                float vw = ImGui::CalcTextSize(vbuf).x;
                dl->AddText(ImVec2(rp.x + rw - vw, rp.y + 2.0f * u),
                            u32(en ? kAmber : kDisabled), vbuf);
                float reserve = ImGui::CalcTextSize("-10000").x + 10.0f * u;
                float tx1 = rp.x + rw - reserve;
                float ty = rp.y + fh + 6.0f * u;
                float frac = (v + 10000.0f) / 20000.0f;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                dl->AddRectFilled(ImVec2(rp.x, ty), ImVec2(tx1, ty + 3.0f * u), u32(kTxtShadow));
                if (frac > 0.0f)
                    dl->AddRectFilled(ImVec2(rp.x, ty), ImVec2(rp.x + (tx1 - rp.x) * frac, ty + 3.0f * u),
                                      u32(kOrange));
                float kx = rp.x + (tx1 - rp.x) * frac;
                dl->AddRectFilled(ImVec2(kx - 2.0f * u, ty - 1.0f * u), ImVec2(kx + 2.0f * u, ty + 4.0f * u),
                                  u32(kSelShadow));
                dl->AddRectFilled(ImVec2(kx - 1.0f * u, ty), ImVec2(kx + 1.0f * u, ty + 3.0f * u),
                                  u32(kWhite));
                if (ImGui::IsItemFocused()) {
                    /* The old steps: 10 a press, 40 once the key
                     * has been held ~40 polls, 160 past ~90 (input
                     * polls run at the game's frame rate). */
                    float step = g_hold_l > 1.5f || g_hold_r > 1.5f ? 160.0f
                               : g_hold_l > 0.66f || g_hold_r > 0.66f ? 40.0f : 10.0f;
                    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true))
                        r->setf(r->arg, v - step);
                    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true))
                        r->setf(r->arg, v + step);
                }
                break;
            }
            default:
                break;
            }
            if (!en) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopID();  // page
        if (g_restore_row >= 0 && !modal_up && ++g_restore_age >= 2) {
            /* The restore target stayed disabled or absent for two
             * frames (a gate closed on the remembered row): fall
             * back to the page's first enabled row rather than
             * leaving the pad with no focus. Frozen while a modal
             * is up, like the restore itself: a modal can outlast
             * the two frames, and ageing here would retarget the
             * pending restore to the first row mid-dialog. */
            g_restore_row = g_first_enabled;
            g_restore_age = 0;
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (focused_now >= 0) {
        g_focus_row = focused_now;
        dk_menu_set_focus_row(focused_now);
    }
    /* ── The help pane: the focused row's own words ── */
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kHelpFace);
    ImGui::PushStyleColor(ImGuiCol_Border, kRowsEdge);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * u, 12.0f * u));
    ImGui::BeginChild("##help", ImVec2(0.0f, body_h), ImGuiChildFlags_Borders);
    ImGui::PushFont(nullptr, 15.0f * u);  // the pane speaks a step quieter than the rows
    {
        int hi = focused_now >= 0 ? focused_now : g_focus_row;
        const DkRow *hr = hi >= 0 && hi < pg.nrows ? &rows[hi] : nullptr;
        if (dk_menu_skin_ok()) {
            plate("##HELP", ImVec2(dk_menu_game_text_width("HELP", 11.0f * u) + 16.0f * u, 26.0f * u), kCyan, kInk);
            ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
            float tw = dk_menu_game_text_width("HELP", 11.0f * u);
            dk_menu_draw_game_text(ImGui::GetWindowDrawList(),
                                   ImVec2((rmin.x + rmax.x) * 0.5f - tw * 0.5f,
                                          (rmin.y + rmax.y) * 0.5f - 5.5f * u),
                                   "HELP", 11.0f * u, u32(kInk));
        } else {
            plate("HELP", ImVec2(ImGui::CalcTextSize("HELP").x + 16.0f * u, 26.0f * u), kCyan, kInk);
        }
        if (hr) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f * u));
            ImGui::PushTextWrapPos(0.0f);
            if (dk_menu_skin_ok() && dk_menu_game_text_covered(hr->label)) {
                ImGui::PushFont(nullptr, 17.0f * u);
                float lh = ImGui::GetTextLineHeight();
                ImGui::PopFont();
                dk_menu_draw_game_text(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                                       hr->label, 13.0f * u, u32(kHelpName));
                ImGui::Dummy(ImVec2(0.0f, lh));
            } else {
                ImGui::PushFont(nullptr, 17.0f * u);
                ImGui::TextColored(kHelpName, "%s", hr->label);
                ImGui::PopFont();
            }
            if (hr->tip) {
                ImGui::Dummy(ImVec2(0.0f, 4.0f * u));
                ImGui::TextColored(kHelpBody, "%s", hr->tip);
            }
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    /* ── The footer: a chip per key, its job after it ── */
    {
        auto chip = [&](const char *key, const char *label, ImVec4 bg, ImVec4 fg) {
            plate(key, ImVec2(ImGui::CalcTextSize(key).x + 12.0f * u, 26.0f * u), bg, fg);
            ImGui::SameLine(0.0f, 6.0f * u);
            ImGui::TextColored(kFoot, "%s", label);
            ImGui::SameLine(0.0f, 16.0f * u);
        };
        ImGui::PushFont(nullptr, 15.0f * u);  // the key line, a step quieter like the help pane
        {
            /* A hairline sets the hint bar off from the panels
             * above it; drawn, not laid out, over the footer's own
             * reserved height. */
            ImVec2 fp = ImGui::GetCursorScreenPos();
            float fw = ImGui::GetContentRegionAvail().x;
            wdl->AddLine(fp, ImVec2(fp.x + fw, fp.y), u32(kRuleSoft), 1.0f);
            ImGui::Dummy(ImVec2(0.0f, 7.0f * u));
        }
        if (dk_menu_skin_ok()) {
            /* Build H: the skin footer is drawn, not laid out, so
             * the whole strip shares one geometry -- every item
             * (game tile or text chip) is the tiles' own 24u tall,
             * every label sits at the item's exact centre, and the
             * spacing is one rhythm throughout (6u item to word,
             * 16u group to group, 2u inside the L R pair). The
             * laid-out version mixed 26u Button chips with 24u
             * images on a shared top edge, and the Button's label
             * placement left DPAD riding left in its chip
             * (Brandon's crop). The job words take the brightened
             * footer grey (kFootBright) for the teal field. */
            const float ih = 24.0f * u;
            ImVec2 fp0 = ImGui::GetCursorScreenPos();
            const float cy = fp0.y + ih * 0.5f;
            const float lh = ImGui::GetTextLineHeight();
            float x = fp0.x;
            const ImU32 border = u32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
            const float round = ImGui::GetStyle().FrameRounding;
            auto text_chip = [&](const char *key) {
                float tw = ImGui::CalcTextSize(key).x;
                float w = tw + 16.0f * u;
                wdl->AddRectFilled(ImVec2(x, fp0.y), ImVec2(x + w, fp0.y + ih), u32(kKeyFace), round);
                wdl->AddRect(ImVec2(x, fp0.y), ImVec2(x + w, fp0.y + ih), border, round, 1.0f, 0);
                wdl->AddText(ImVec2(x + (w - tw) * 0.5f, cy - lh * 0.5f), u32(kKeyText), key);
                x += w;
            };
            auto tile = [&](DkSkinSprite s) {
                ImVec2 uv0, uv1;
                dk_menu_skin_sprite_uv(s, uv0, uv1);
                wdl->AddImage(dk_menu_skin_atlas_ref(), ImVec2(x, fp0.y), ImVec2(x + ih, fp0.y + ih), uv0, uv1);
                x += ih;
            };
            auto job = [&](const char *label) {
                wdl->AddText(ImVec2(x + 6.0f * u, cy - lh * 0.5f), u32(kFootBright), label);
                x += 6.0f * u + ImGui::CalcTextSize(label).x + 16.0f * u;
            };
            /* The game's own button tiles where the sheet has
             * them (A, B, L, R); the other keys keep their text
             * chips -- the sheet carries no dpad or minus art. */
            text_chip("DPAD");
            job("MOVE");
            tile(DK_SPR_TILE_A);
            job("SELECT");
            tile(DK_SPR_TILE_B);
            job("BACK");
            tile(DK_SPR_TILE_L);
            x += 2.0f * u;
            tile(DK_SPR_TILE_R);
            job("TABS");
            /* The button that opens and closes the menu itself
             * (Minus: a tap, a half-second hold, or L+R+Minus --
             * xinput_device.c's menu_button_tick). Display only,
             * like the other chips. */
            text_chip("-");
            job("CLOSE");
            ImGui::Dummy(ImVec2(0.0f, ih));
        } else {
            chip("DPAD", "MOVE", kKeyFace, kKeyText);
            chip("A", "SELECT", kCyan, kInk);
            chip("B", "BACK", kMagenta, kInk);
            chip("L R", "TABS", kKeyFace, kKeyText);
            /* The button that opens and closes the menu itself (Minus:
             * a tap, a half-second hold, or L+R+Minus -- xinput_device.c's
             * menu_button_tick). Display only, like the other chips. */
            chip("-", "CLOSE", kKeyFace, kKeyText);
        }
        ImGui::PopFont();
    }
    ImGui::PopFont();
    ImGui::End();

    /* ── The choice list: an ImGui popup under its row. Browsing
     * commits nothing extra -- the row's own set() is the commit,
     * which for the music row IS the commit (left/right only stage)
     * and for a district opens the jump confirmation. ── */
    if (g_choice_row >= 0 && g_choice_row < pg.nrows && !modal_up) {
        /* !modal_up: the list never opens over a dialog. A row can
         * only be activated while it holds the pad's focus, and
         * while a modal is up the focus lives in the dialog (see
         * note_focus and the modal block), so this is a guard, not
         * a path -- but if a press ever did land here, opening the
         * list would close the modal in the popup stack
         * (OpenPopupEx closes back to the caller's level) while the
         * core still holds it armed and unanswered. */
        const DkRow *r = &rows[g_choice_row];
        /* Open once, on the activation frame only. The previous
         * shape re-opened the popup on any frame it found closed,
         * which undid a nav Cancel in the same frame it landed:
         * the list could be picked from, but never backed out of. */
        if (g_choice_fresh)
            ImGui::OpenPopup("dkchoice");
        ImGui::SetNextWindowPos(ImVec2(g_choice_anchor.x - g_choice_w, g_choice_anchor.y + 2.0f * u),
                                ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(g_choice_w, 0.0f), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * u, 10.0f * u));
        if (ImGui::BeginPopup("dkchoice")) {
            ImGui::PushFont(nullptr, 17.0f * u);
            const unsigned flags = dk_menu_row_flags(r);
            int nc = dk_menu_row_nchoices(r);
            int cur = flags & DK_ROW_F_MUSIC ? dk_menu_music_stage()
                    : flags & DK_ROW_F_TRACK ? dk_menu_track_stage()
                    : flags & DK_ROW_F_CUSTOM ? dk_menu_custom_stage()
                    : r->get(r->arg);
            for (int j = 0; j < nc; j++) {
                bool sel = j == cur;
                if (sel) {
                    ImGui::PushStyleColor(ImGuiCol_Header, kOrange);
                    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
                }
                if (ImGui::Selectable(r->choices[j], sel)) {
                    r->set(r->arg, j);
                    ImGui::CloseCurrentPopup();
                    g_restore_row = g_choice_row;  // focus returns to the row
                    g_restore_age = 0;
                    g_choice_row = -1;
                }
                if (sel) {
                    ImGui::PopStyleColor(2);
                    if (g_choice_fresh) ImGui::SetItemDefaultFocus();
                }
            }
            g_choice_fresh = false;
            ImGui::PopFont();
            ImGui::EndPopup();
        } else {
            /* Dismissed (nav Cancel): nothing committed, and the
             * pad's focus goes back to the row that opened the list
             * instead of being left wherever the popup dropped it. */
            g_restore_row = g_choice_row;
            g_restore_age = 0;
            g_choice_row = -1;
            g_choice_fresh = false;
        }
        ImGui::PopStyleVar();
    }

    /* ── A modal, over the panel: caution stripe, title plate,
     * body, and the two buttons the core resolves. ── */
    {
        int action = dk_menu_modal_action();
        if (action != DK_ACT_NONE) {
            if (g_modal_shown != action) {
                ImGui::OpenPopup("dkmodal");
                g_modal_shown = action;
                g_modal_fresh = true;
            }
            float mw = pw * 0.64f;
            ImGui::SetNextWindowPos(ImVec2((fb_w - mw) * 0.5f, fb_h * 0.5f), ImGuiCond_Appearing,
                                    ImVec2(0.0f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(mw, 0.0f), ImGuiCond_Appearing);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * u, 16.0f * u));
            if (ImGui::BeginPopupModal("dkmodal", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::PushFont(nullptr, 17.0f * u);
                ImDrawList *dl = ImGui::GetWindowDrawList();
                {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float w = ImGui::GetContentRegionAvail().x;
                    float bh = 6.0f * u, bw = 18.0f * u;
                    for (int row2 = 0; row2 < 2; row2++) {
                        int k = row2;
                        for (float x = -row2 * bw * 0.5f; x < w; x += bw, k++) {
                            float x0 = x < 0.0f ? 0.0f : x, x1 = x + bw > w ? w : x + bw;
                            if (x1 > x0)
                                dl->AddRectFilled(ImVec2(p.x + x0, p.y + row2 * bh),
                                                  ImVec2(p.x + x1, p.y + (row2 + 1) * bh),
                                                  u32(k % 2 == 0 ? kOrange : kHazardInk));
                        }
                    }
                    ImGui::Dummy(ImVec2(w, 2.0f * bh + 10.0f * u));
                }
                {
                    const char *t = dk_menu_modal_title();
                    plate(t, ImVec2(ImGui::CalcTextSize(t).x + 16.0f * u, 30.0f * u), kOrange, kInk);
                }
                ImGui::Dummy(ImVec2(0.0f, 8.0f * u));
                ImGui::TextWrapped("%s", dk_menu_modal_body());
                ImGui::Dummy(ImVec2(0.0f, 12.0f * u));
                const char *ok = action == DK_ACT_CHEAT_WARNING ? "A  I UNDERSTAND" : "A  CONFIRM";
                const char *no = action == DK_ACT_CHEAT_WARNING ? "B  GO BACK" : "B  CANCEL";
                bool close = false;
                ImGui::PushStyleColor(ImGuiCol_Button, kOrange);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kOrange);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kOrange);
                ImGui::PushStyleColor(ImGuiCol_Text, kInk);
                if (ImGui::Button(ok, ImVec2(ImGui::CalcTextSize(ok).x + 20.0f * u, 34.0f * u))) {
                    dk_menu_modal_confirm();
                    close = true;
                }
                const ImGuiID ok_id = ImGui::GetItemID();
                const bool ok_focused = ImGui::IsItemFocused();
                if (g_modal_fresh) {
                    ImGui::SetItemDefaultFocus();
                    g_modal_fresh = false;
                }
                ImGui::PopStyleColor(4);
                float no_w = ImGui::CalcTextSize(no).x + 20.0f * u;
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - no_w);
                ImGui::PushStyleColor(ImGuiCol_Button, kChipFace);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kActFace);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kChipFace);
                ImGui::PushStyleColor(ImGuiCol_Text, kMagenta);
                if (ImGui::Button(no, ImVec2(no_w, 34.0f * u))) {
                    dk_menu_modal_cancel();
                    close = true;
                }
                const bool no_focused = ImGui::IsItemFocused();
                ImGui::PopStyleColor(4);
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
                    dk_menu_modal_cancel();
                    close = true;
                }
                /* The dialog is modal for the pad, not just in name:
                 * while it is up, the pad's focus lives on its own
                 * buttons. If neither holds it (the nav init went
                 * elsewhere, or focus was moved behind the dialog),
                 * put it on the confirm button outright, the same
                 * SetFocusID the rows' restore uses -- inside the
                 * dialog's own window, so nothing behind it can be
                 * navigated or activated until it closes. Self-
                 * limiting: once a button holds focus this stops. */
                if (!close && !ok_focused && !no_focused && ok_id != 0)
                    ImGui::SetFocusID(ok_id, ImGui::GetCurrentWindow());
                if (close) {
                    ImGui::CloseCurrentPopup();
                    g_modal_shown = DK_ACT_NONE;
                }
                ImGui::PopFont();
                ImGui::EndPopup();
            } else if (!ImGui::IsPopupOpen("dkmodal")) {
                /* The popup was closed behind our back (the popup
                 * stack was cut by another open) while the core
                 * still holds the modal armed: forget that we showed
                 * it, so it opens again next frame instead of
                 * staying armed and invisible for good. */
                g_modal_shown = DK_ACT_NONE;
            }
            ImGui::PopStyleVar();
        } else {
            g_modal_shown = DK_ACT_NONE;
        }
    }
}

}  // namespace

/* FLAG -- INIT TIMING (needs a hardware A/B): everything init()
 * does -- the ImGui context, the pl shared-font load, the backend's
 * shader compiles and descriptor memory -- used to happen lazily at
 * the first menu open, mid-game, on the renderer thread: the known
 * first-open hitch. dk_imgui_menu_warm() runs the same one-time
 * init early instead, called by the renderer on its first present
 * (loading-screen time; present() running at all is the proof the
 * device, queue and shader services the backend init uses are up).
 * It is guarded by the same g_init/g_broken pair as the frame path,
 * so an opened menu finds the work done and dk_imgui_menu_frame is
 * otherwise unchanged. What the A/B must prove on hardware: that
 * init this early is as safe as init at first open (nothing in the
 * backend init depends on a presented frame -- it allocates and
 * compiles only -- but no run has exercised it this early), and
 * that the hitch is gone from the first open. */
void dk_imgui_menu_warm(void)
{
    if (g_broken) return;
    if (!g_init) {
        g_init = true;
        init();
    }
}

void dk_imgui_menu_frame(uint32_t buttons, int fb_w, int fb_h)
{
    if (g_broken) return;
    if (!g_init) {
        g_init = true;
        if (!init()) return;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(fb_w), float(fb_h));
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = double(ts.tv_sec) + double(ts.tv_nsec) / 1e9;
        /* Frames only run while the menu is open, so a long gap
         * between calls means the menu was shut and reopened:
         * build_menu re-arms the focus restore for it. */
        if (g_last_t > 0.0 && now - g_last_t > 0.25) g_reopen = true;
        io.DeltaTime = g_last_t > 0.0 ? float(now - g_last_t) : 1.0f / 60.0f;
        if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;
        g_last_t = now;
    }
    /* The slider keys' hold time (renderer frames, not input polls;
     * at the game's frame rate they are the same count). */
    g_hold_l = buttons & DK_MENU_LEFT ? g_hold_l + io.DeltaTime : 0.0f;
    g_hold_r = buttons & DK_MENU_RIGHT ? g_hold_r + io.DeltaTime : 0.0f;
    feed_input(io, buttons);
    ImGui::NewFrame();
    build_menu(fb_w, fb_h);
    ImGui::Render();
    dk_imgui_render(ImGui::GetDrawData(), fb_w, fb_h);
}

#else  /* !XBOXRECOMP_DK_MENU */

void dk_imgui_menu_frame(uint32_t, int, int) {}
void dk_imgui_menu_warm(void) {}

#endif
