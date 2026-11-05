// The settings menu's game-assets skin: JSRF's own UI art, loaded
// at runtime from the player's own game files and drawn through
// the menu's ImGui path (see nv2a_dk_menu_skin.cpp for the loader
// and the fallback rules). Nothing from the game ships in the
// build; when the art cannot be loaded, dk_menu_skin_ok() stays
// false and the menu renders exactly as it did before the skin.

#pragma once

#include "imgui.h"

/* Load the game art and register it with ImGui. Called once from
 * the menu's init(); failure is not an error -- the skin simply
 * stays off. */
bool dk_menu_skin_init(void);
bool dk_menu_skin_ok(void);

/* Sprites in the one packed atlas texture. */
enum DkSkinSprite {
    DK_SPR_BADGE = 0,  // the JSRF pentagon badge (TitleE)
    DK_SPR_PANEL,      // the message-window panel, border and all (SprNorm)
    DK_SPR_PANEL_IN,   // the same panel's borderless interior (tab plates)
    DK_SPR_STATIC,     // the TV-static patch (the spray band)
    DK_SPR_CAN,        // the spray can (the selection cursor)
    DK_SPR_TILE_A,     // the game's button tiles (the footer)
    DK_SPR_TILE_B,
    DK_SPR_TILE_L,
    DK_SPR_TILE_R,
    DK_SPR_COUNT
};

ImTextureRef dk_menu_skin_atlas_ref(void);
void dk_menu_skin_sprite_uv(DkSkinSprite s, ImVec2 &uv0, ImVec2 &uv1);
ImVec2 dk_menu_skin_sprite_px(DkSkinSprite s);  // native size in atlas pixels

/* The panel texture as a 9-sliced framed wash over a rect.
 * corner is the destination corner size in pixels. */
void dk_menu_draw_panel_wash(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float alpha, float corner);

/* The game's UI font (jetfont's Latin page): a fixed grid, cut by
 * the measured ink runs. For heads, tabs and labels only -- its
 * coverage is codes 0x21..0x74 (no lowercase u-z), so body text
 * never uses it. covered() gates every call site: a string with a
 * character outside the page keeps the caller's normal text path. */
bool dk_menu_game_text_covered(const char *s);
float dk_menu_game_text_width(const char *s, float ink_h);
/* Draws s top-aligned at pos, tinted col, at the given nominal ink
 * height; returns the pen advance in pixels. */
float dk_menu_draw_game_text(ImDrawList *dl, ImVec2 pos, const char *s, float ink_h, ImU32 col);
