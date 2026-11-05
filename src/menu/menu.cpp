/*
 * menu.cpp -- draws the rows added with xbox_MenuAdd (menu.h).
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as Dear
 * ImGui is: nothing here may need the C++ runtime library (no new, no STL),
 * so the engine still links against libc alone.
 */
#include "menu.h"
#include "imgui.h"
#include "imgui_internal.h"   /* the bar's nav state: menu_build */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The window's frame rate for the bar (nv2a_gl.c), weak as About's
 * version is: a window without it simply shows no rate. */
extern "C" double nv2a_menu_frame_rate(void) __attribute__((weak));

static const char *const k_menu_title[] = {
    "File", "Display", "Graphics", "Controller", "Cheats", "Music", "Developer"
};
static_assert(IM_ARRAYSIZE(k_menu_title) == XBOX_MENU_COUNT, "a title for each menu in menu.h");

/* The rows, and the boxes open over them -- by index: rows still arriving
 * can move them. The window's thread only. */
static ImVector<XBOX_MENU_ITEM> g_items;
static int g_ask = -1;          /* waiting for OK: its confirm text */
static int g_ask_pick;          /* ... and the choice, for a CHOICE or TOGGLES */
static int g_info = -1;         /* an INFO box to open */
static int g_help = -1;          /* row whose description is shown */
static int g_active_menu = -1;   /* selected tab, kept while the menu is shut */
static int g_tab_request = -1;   /* next tab selected by L / R */

/* Rows come from any thread -- the input layer's, the game's -- and are drawn
 * on the window's, so xbox_MenuAdd only queues them, and each frame takes the
 * queue first. The lock is held for a copy and nothing else, so a spin is
 * enough; the queue is the C library's, not ImGui's, which is not meant for
 * other threads. */
static XBOX_MENU_ITEM *g_queued;
static int             g_nqueued;
static char            g_queue_lock;

static void queue_lock(void)   { while (__atomic_test_and_set(&g_queue_lock, __ATOMIC_ACQUIRE)) { } }
static void queue_unlock(void) { __atomic_clear(&g_queue_lock, __ATOMIC_RELEASE); }

extern "C" void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    XBOX_MENU_ITEM *q;
    if (count <= 0) return;
    queue_lock();
    q = (XBOX_MENU_ITEM *)realloc(g_queued, (size_t)(g_nqueued + count) * sizeof *q);
    if (q) {
        memcpy(q + g_nqueued, items, (size_t)count * sizeof *q);
        g_queued = q;
        g_nqueued += count;
    }
    queue_unlock();
    if (!q) fprintf(stderr, "[MENU] out of memory: %d rows not added\n", count);
}

static void take_queued(void)
{
    queue_lock();
    for (int i = 0; i < g_nqueued; i++)
        g_items.push_back(g_queued[i]);
    g_nqueued = 0;
    queue_unlock();
}

static void apply(const XBOX_MENU_ITEM *it, int pick)
{
    if (it->enabled && !it->enabled()) return;
    if (pick >= 0) {
        if (it->pick) it->pick(pick);
    } else if (it->run) {
        it->run();
    }
}

/* Chosen: straight away, or once confirmed. The box is opened from the root
 * window below, so a nested combo cannot dismiss it. */
static void chosen(const XBOX_MENU_ITEM *it, int pick)
{
    if (it->enabled && !it->enabled()) return;
    if (it->confirm) { g_ask = g_items.index_from_ptr(it); g_ask_pick = pick; }
    else             apply(it, pick);
}

/* A NUMBER's - and +: values between steps go to the next step either way. */
#define MENU_STEP_SLACK 1e-6

static double step_from(const XBOX_MENU_ITEM *it, double v, int dir)
{
    double n;
    if (!isfinite(v) || !isfinite(it->min) || !isfinite(it->max)
     || !isfinite(it->step) || it->step <= 0.0 || it->max < it->min)
        return v;
    n = (v - it->min) / it->step;
    if (!isfinite(n)) return v;
    n = dir > 0 ? floor(n + MENU_STEP_SLACK) + 1 : ceil(n - MENU_STEP_SLACK) - 1;
    v = it->min + n * it->step;
    return v < it->min ? it->min : v > it->max ? it->max : v;
}

/* Tips appear on hover/focus and remain visible in the side help panel. */
static void item_tip(const XBOX_MENU_ITEM *it)
{
    if (!it->tip) return;
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(it->tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void note_item(const XBOX_MENU_ITEM *it)
{
    if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
        g_help = g_items.index_from_ptr(it);
    item_tip(it);
}

static int menu_has_items(int m)
{
    for (const XBOX_MENU_ITEM &it : g_items)
        if ((int)it.menu == m) return 1;
    return 0;
}

static int first_menu_with_items(void)
{
    for (int m = 0; m < XBOX_MENU_COUNT; m++)
        if (menu_has_items(m)) return m;
    return -1;
}

static int first_item_in_menu(int m)
{
    for (int i = 0; i < g_items.Size; i++)
        if ((int)g_items[i].menu == m && g_items[i].kind != XBOX_ITEM_SEPARATOR)
            return i;
    return -1;
}

static int neighbor_menu(int current, int direction)
{
    if (current < 0 || current >= XBOX_MENU_COUNT)
        return first_menu_with_items();
    for (int step = 1; step <= XBOX_MENU_COUNT; step++) {
        int m = (current + XBOX_MENU_COUNT + direction * step) % XBOX_MENU_COUNT;
        if (menu_has_items(m)) return m;
    }
    return current;
}

static void draw_item_label(const XBOX_MENU_ITEM *it)
{
    if (it->label) ImGui::TextWrapped("%s", it->label);
    else            ImGui::TextDisabled("Unavailable");
    note_item(it);
}

static void draw_item(const XBOX_MENU_ITEM *it)
{
    int index = g_items.index_from_ptr(it);
    ImGui::TableNextRow();
    if (it->kind == XBOX_ITEM_SEPARATOR) {
        ImGui::TableSetColumnIndex(0);
        ImGui::Separator();
        return;
    }

    ImGui::PushID(index);
    ImGui::TableSetColumnIndex(0);
    draw_item_label(it);
    ImGui::TableSetColumnIndex(1);

    switch (it->kind) {
    case XBOX_ITEM_ACTION:
        ImGui::BeginDisabled(it->run == NULL || (it->enabled && !it->enabled()));
        if (ImGui::Button("Run", ImVec2(-1.0f, 0.0f))) chosen(it, -1);
        ImGui::EndDisabled();
        note_item(it);
        break;
    case XBOX_ITEM_TOGGLE: {
        bool on = it->state ? it->state() != 0 : false;
        ImGui::BeginDisabled(it->run == NULL || it->state == NULL
                          || (it->enabled && !it->enabled()));
        if (ImGui::Checkbox("##value", &on)) chosen(it, -1);
        ImGui::EndDisabled();
        note_item(it);
        break;
    }
    case XBOX_ITEM_CHOICE: {
        int cur = it->state ? it->state() : -1;
        const char *preview = it->choices && cur >= 0 && cur < it->nchoices
                            ? it->choices[cur] : cur < 0 ? "Choose an option" : "Unavailable";
        ImGui::BeginDisabled(it->pick == NULL || it->choices == NULL || it->nchoices <= 0
                          || (it->enabled && !it->enabled()));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##value", preview)) {
            for (int i = 0; i < it->nchoices; i++) {
                bool selected = i == cur;
                if (ImGui::Selectable(it->choices[i], selected) && !selected)
                    chosen(it, i);
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        note_item(it);
        break;
    }
    case XBOX_ITEM_TOGGLES: {
        uint32_t mask = (uint32_t)(it->state ? it->state() : 0);
        int count = 0, n = it->nchoices < 32 ? it->nchoices : 32;
        char preview[32];
        for (int i = 0; i < n; i++)
            if (mask & (1u << i)) count++;
        snprintf(preview, sizeof preview, "%d selected", count);
        ImGui::BeginDisabled(it->pick == NULL || it->choices == NULL || n <= 0
                          || (it->enabled && !it->enabled()));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##value", preview)) {
            for (int i = 0; i < n; i++) {
                bool on = (mask & (1u << i)) != 0;
                if (ImGui::MenuItem(it->choices[i], NULL, on)) chosen(it, i);
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        note_item(it);
        break;
    }
    case XBOX_ITEM_INFO:
        ImGui::BeginDisabled(it->text == NULL || (it->enabled && !it->enabled()));
        if (ImGui::Button("View", ImVec2(-1.0f, 0.0f))) g_info = index;
        ImGui::EndDisabled();
        note_item(it);
        break;
    case XBOX_ITEM_NUMBER: {
        double value = it->value ? it->value() : 0.0, next = value;
        char text[32];
        float side = ImGui::GetFrameHeight();
        bool valid = it->value && it->set && it->format
                  && isfinite(value) && isfinite(it->min) && isfinite(it->max)
                  && isfinite(it->step) && it->step > 0.0 && it->max >= it->min
                  && (!it->enabled || it->enabled());
        if (!valid) {
            ImGui::TextDisabled("Unavailable");
        } else {
            snprintf(text, sizeof text, it->format, value);
            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
            if (ImGui::Button("-", ImVec2(side, side))) next = step_from(it, value, -1);
            ImGui::SameLine();
            ImGui::BeginDisabled(true);
            ImGui::Button(text, ImVec2(ImGui::GetFontSize() * 6.0f, side));
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("+", ImVec2(side, side))) next = step_from(it, value, 1);
            ImGui::PopItemFlag();
            if (next != value) it->set(next);
        }
        note_item(it);
        break;
    }
    case XBOX_ITEM_SEPARATOR:
        break;
    }
    ImGui::PopID();
}

static const char *const k_menu_summary[] = {
    "About the port and actions for this session.",
    "Output scale and frame pacing.",
    "Aspect ratio, edge smoothing and image quality.",
    "Camera, button layout and game-menu controls.",
    "Use at your own risk. Session cheats are separate from progress changes.",
    "Choose a DJ mix for the next time the game opens one.",
    "Logging, profiling and advanced runtime options."
};
static_assert(IM_ARRAYSIZE(k_menu_summary) == XBOX_MENU_COUNT,
              "a description for each menu page");

static void draw_menu_page(int m)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    float footer_h = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;
    ImVec2 avail;
    float help_w, options_w, body_h;
    int help_index;

    if (g_help < 0 || g_help >= g_items.Size || (int)g_items[g_help].menu != m)
        g_help = first_item_in_menu(m);
    ImGui::TextWrapped("%s", k_menu_summary[m]);
    ImGui::Separator();
    avail = ImGui::GetContentRegionAvail();
    body_h = avail.y - footer_h;
    if (body_h < 0.0f) body_h = 0.0f;
    help_w = avail.x * 0.32f;
    if (help_w < ImGui::GetFontSize() * 10.0f) help_w = ImGui::GetFontSize() * 10.0f;
    if (help_w > avail.x * 0.45f) help_w = avail.x * 0.45f;
    options_w = avail.x - help_w - style.ItemSpacing.x;
    if (options_w < 0.0f) options_w = 0.0f;

    ImGui::BeginChild("##options", ImVec2(options_w, body_h), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingStretchProp
                                       | ImGuiTableFlags_RowBg
                                       | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Option", ImGuiTableColumnFlags_WidthStretch, 1.45f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (int pass = 0; pass < 2; pass++) {
            const char *last_section = NULL;
            for (const XBOX_MENU_ITEM &it : g_items) {
                if ((int)it.menu != m || (it.last != 0) != (pass == 1)) continue;
                if (it.section && (!last_section || strcmp(last_section, it.section) != 0)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SeparatorText(it.section);
                }
                last_section = it.section;
                draw_item(&it);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    help_index = g_help;
    ImGui::BeginChild("##help", ImVec2(0.0f, body_h), true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("HELP");
    ImGui::Separator();
    if (help_index >= 0 && help_index < g_items.Size
     && (int)g_items[help_index].menu == m) {
        const XBOX_MENU_ITEM *it = &g_items[help_index];
        if (it->section) ImGui::TextDisabled("%s", it->section);
        if (it->label) ImGui::TextWrapped("%s", it->label);
        ImGui::Spacing();
        if (it->tip) ImGui::TextWrapped("%s", it->tip);
        else ImGui::TextDisabled("No description is available for this option.");
    } else {
        ImGui::TextWrapped("Select an option to see what it does.");
    }
    ImGui::EndChild();
}

/* Every box sits in the middle of the window, and stays there: put back each
 * frame, so it follows the window when that changes size. */
static void box_centre(void)
{
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
}

/* A box over the menu: as big as what it says, and not to be dragged off. */
static bool box_begin(const char *title)
{
    box_centre();
    return ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
}

extern "C" void menu_build(int just_opened)
{
    ImGuiContext &g = *ImGui::GetCurrentContext();
    /* B or Esc backs out of a popup; without one it closes the overlay. */
    bool back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_NavGamepadCancel, false);
    bool popup_open;
    int m;
    const XBOX_MENU_ITEM *ask, *info;
    bool got_page = false;

    take_queued();
    /* Hidden ImGui popups survive between frames. Close them on every open
     * and restore the last selected tab, or start on Display. */
    if (just_opened) {
        if (!menu_has_items(g_active_menu))
            g_active_menu = menu_has_items(XBOX_MENU_DISPLAY)
                          ? XBOX_MENU_DISPLAY : first_menu_with_items();
        g_tab_request = g_active_menu;
        g_help = first_item_in_menu(g_active_menu);
        if (g.OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, false);
        g_ask = g_info = -1;
    }
    popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (back && !popup_open && g_ask < 0 && g_info < 0)
        xbox_MenuRequestClose();
    if (!popup_open && g_ask < 0 && g_info < 0) {
        bool left = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false);
        bool right = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false);
        if (left != right) g_tab_request = neighbor_menu(g_active_menu, right ? 1 : -1);
    }

    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize,
                                                   IM_COL32(0, 0, 0, 150));
    {
        const ImGuiIO &io = ImGui::GetIO();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
                               | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.84f, io.DisplaySize.y * 0.84f),
                                 ImGuiCond_Always);
        if (ImGui::Begin("##settings", NULL, flags)) {
            ImGui::TextUnformatted("JSRF UNLEASHED");
            ImGui::SameLine();
            ImGui::TextDisabled("SETTINGS");
            ImGui::Separator();
            if (ImGui::BeginTabBar("##pages", ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (m = 0; m < XBOX_MENU_COUNT; m++) {
                    if (!menu_has_items(m)) continue;
                    ImGuiTabItemFlags tab_flags = m == g_tab_request
                                                  ? ImGuiTabItemFlags_SetSelected : 0;
                    if (ImGui::BeginTabItem(k_menu_title[m], NULL, tab_flags)) {
                        got_page = true;
                        g_active_menu = m;
                        if (m == g_tab_request) ImGui::SetItemDefaultFocus();
                        if (g_help < 0 || g_help >= g_items.Size
                         || (int)g_items[g_help].menu != m)
                            g_help = first_item_in_menu(m);
                        ImGui::PushID(m);
                        draw_menu_page(m);
                        ImGui::PopID();
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            g_tab_request = -1;
            if (!got_page) g_tab_request = first_menu_with_items();
            ImGui::Separator();
            const char *hint =
#if defined(__SWITCH__)
                "L / R Tabs    A Select    B Back    - Close";
#else
                "F1 / Back Close    Enter Select    Esc Back";
#endif
            double rate = nv2a_menu_frame_rate ? nv2a_menu_frame_rate() : 0.0;
            ImGui::TextDisabled("%s", hint);
            if (rate > 0.0) {
                char buf[16];
                snprintf(buf, sizeof buf, "%.0f fps", rate);
                float x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x
                        - ImGui::CalcTextSize(buf).x;
                if (x > ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x) {
                    ImGui::SameLine(x);
                    ImGui::TextDisabled("%s", buf);
                }
            }
        }
        ImGui::End();
    }

    ask  = g_ask  >= 0 && g_ask  < g_items.Size ? &g_items[g_ask]  : NULL;
    info = g_info >= 0 && g_info < g_items.Size ? &g_items[g_info] : NULL;
    if (ask && !ImGui::IsPopupOpen("Are you sure?")) ImGui::OpenPopup("Are you sure?");
    if (info && !ImGui::IsPopupOpen(info->label)) ImGui::OpenPopup(info->label);

    if (ask && box_begin("Are you sure?")) {
        const char *target = ask->label;
        if (g_ask_pick >= 0 && ask->choices && g_ask_pick < ask->nchoices)
            target = ask->choices[g_ask_pick];
        ImGui::TextWrapped("%s", target ? target : "Selected option");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ask->confirm ? ask->confirm : "Are you sure?");
        ImGui::Spacing();
        bool ok = ImGui::Button("OK");
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel");
        ImGui::SetItemDefaultFocus();   /* a pad starts on Cancel: OK cannot be undone */
        if (ok) apply(ask, g_ask_pick);
        if (ok || cancel || back) { g_ask = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (info && box_begin(info->label)) {
        const char *text = info->text ? info->text() : NULL;
        ImGui::TextWrapped("%s", text ? text : "No details are available.");
        ImGui::Spacing();
        bool ok = ImGui::Button("OK");
        ImGui::SetItemDefaultFocus();
        if (ok || back) { g_info = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

/* The frame rate box: small, in the top-left corner, see-through, and
 * never taking input or focus from the game or the menu. */
extern "C" void menu_build_corner(const char *text)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    ImGui::SetNextWindowPos(ImVec2(st.WindowPadding.x, st.WindowPadding.y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    if (ImGui::Begin("##corner", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
        ImGui::TextUnformatted(text);
    ImGui::End();
}

extern "C" void menu_build_busy(const char *what, int done, int total)
{
    const ImGuiIO &io = ImGui::GetIO();
    char line[32];
    box_centre();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.5f, 0.0f), ImGuiCond_Always);
    if (ImGui::Begin("##busy", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted(what);
        snprintf(line, sizeof line, "%d of %d", done, total);
        ImGui::ProgressBar(total > 0 ? (float)done / (float)total : 0.0f, ImVec2(-1.0f, 0.0f), line);
    }
    ImGui::End();
}
