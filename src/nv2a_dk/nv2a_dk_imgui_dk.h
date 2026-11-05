/* nv2a_dk_imgui_dk.h -- the settings menu's Dear ImGui renderer
 * backend for deko3d (nv2a_dk_imgui_dk.cpp). */
#ifndef NV2A_DK_IMGUI_DK_H
#define NV2A_DK_IMGUI_DK_H

struct ImDrawData;

/* Compile the overlay shaders (uam, the route this build compiles
 * every shader by) and set the renderer's ImGui backend flags.
 * Returns false on failure; the menu then stays shut and says why
 * in the log, once. */
bool dk_imgui_backend_init(void);

/* Record ImGui's draw data into the renderer's current command
 * buffer, over the bound swapchain image (ww x wh pixels). Called
 * from present(), after the game picture. No state is saved or
 * restored: the backend binds everything it uses, and the game's
 * next frame rebinds its own (frame_state in nv2a_dk.c), the same
 * convention the old menu's clears relied on. */
void dk_imgui_render(ImDrawData *d, int ww, int wh);

#endif /* NV2A_DK_IMGUI_DK_H */
