/* nv2a_dk_menu_state.h -- the settings menu's data model, shared by
 * the menu core (nv2a_dk_menu.c: settings storage, the row tables,
 * modal actions, the cheat / music / map state machines) and its
 * ImGui front-end (nv2a_dk_menu_imgui.cpp).
 *
 * The front-end owns no settings state of its own. It renders these
 * rows, calls their get/set, and asks the core to move pages and
 * resolve modals, so the behaviours the menu has always had -- music
 * picks staged until the list confirms them, map jumps behind a
 * confirmation, the cheat warning gate on the Cheats page, teleport
 * coordinates seeded from where the player stands -- stay the core's,
 * unchanged. State the front-end needs once a frame (page, modal,
 * shared availability) is sampled through here, once, not per row. */
#ifndef NV2A_DK_MENU_STATE_H
#define NV2A_DK_MENU_STATE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Row kinds. */
enum { DK_R_SECTION, DK_R_INFO, DK_R_CHECK, DK_R_CHOICE, DK_R_BUTTON, DK_R_SLIDER };

typedef struct {
    const char *label;
    int kind;
    const char *tip;
    intptr_t arg;
    int  (*enabled)(intptr_t);
    int  (*get)(intptr_t);
    void (*set)(intptr_t, int);
    const char **choices;
    int nchoices;
    const char *(*value)(intptr_t, char *buf, size_t n);
    float (*getf)(intptr_t);
    void (*setf)(intptr_t, float);
    void (*run)(intptr_t);
} DkRow;
typedef struct { const char *name; const DkRow *rows; int nrows; } DkPage;

/* Modal actions. */
enum { DK_ACT_NONE, DK_ACT_CHEAT_WARNING, DK_ACT_MAP_JUMP, DK_ACT_REDIRECT,
       DK_ACT_REBUILD_PACK, DK_ACT_WARNINGS_OFF, DK_ACT_RESET_ALL };

/* Row flavour flags (dk_menu_row_flags). */
enum {
    DK_ROW_F_MUSIC  = 1u << 0,  /* left/right stage a pick; only the list commits */
    DK_ROW_F_TRACK  = 1u << 3,  /* the Music page's track row: same staging, its own stage */
    DK_ROW_F_CUSTOM = 1u << 4,  /* the Music page's custom-track row: same staging, its own stage */
    DK_ROW_F_DISTRICT = 1u << 5,  /* the Music page's district rows: same staging, a stage per row */
    DK_ROW_F_MAP    = 1u << 1,  /* a district jump: picked from the list, never cycled into */
    DK_ROW_F_TPGATE = 1u << 2,  /* enabled() is the teleport-ready query, shared by its rows */
};

const DkPage *dk_menu_pages(int *count);
int  dk_menu_page(void);
void dk_menu_set_page(int p);
int  dk_menu_focus_row(void);          /* the page's remembered focus row */
void dk_menu_set_focus_row(int row);
int  dk_menu_row_nchoices(const DkRow *r);
unsigned dk_menu_row_flags(const DkRow *r);
int  dk_menu_tp_ready(void);
int  dk_menu_map_avail(void);
int  dk_menu_music_stage(void);
void dk_menu_music_stage_step(const DkRow *r, int d);
int  dk_menu_track_stage(void);
void dk_menu_track_stage_step(const DkRow *r, int d);
int  dk_menu_custom_stage(void);
void dk_menu_custom_stage_step(const DkRow *r, int d);
int  dk_menu_district_stage(const DkRow *r);
void dk_menu_district_stage_step(const DkRow *r, int d);
int  dk_menu_modal_action(void);
const char *dk_menu_modal_title(void);
const char *dk_menu_modal_body(void);
void dk_menu_modal_confirm(void);
void dk_menu_modal_cancel(void);

/* The ImGui front-end's one frame: consume the latched pad bitmask
 * (DK_MENU_* bits, nv2a_dk_menu.h), build the menu and record it
 * into the renderer's command buffer. Renderer thread, called from
 * nv2a_dk_menu_draw() while the menu is open. */
void dk_imgui_menu_frame(uint32_t buttons, int fb_w, int fb_h);

/* Pay the front-end's one-time init (ImGui context, the shared
 * font, the backend's shaders and descriptors) ahead of the first
 * open: the renderer calls this once, on its first present, so the
 * cost lands on the loading screen instead of mid-game at the
 * first open. A no-op once the front-end is up (the same
 * g_init/g_broken guard the frame path uses). Renderer thread. */
void dk_imgui_menu_warm(void);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_DK_MENU_STATE_H */
