/* nv2a_dk_ui.h -- the contract between the deko3d renderer (nv2a_dk.c)
 * and the settings menu's core (nv2a_dk_menu.c).
 *
 * The menu's front-end is Dear ImGui (nv2a_dk_menu_imgui.cpp), drawn
 * from inside present() on the renderer thread by its own deko3d
 * backend (nv2a_dk_imgui_dk.cpp); the backend reaches the renderer's
 * device, command buffer and per-frame data ring through the narrow
 * service bridge in nv2a_dk_imgui_bridge.h. This header carries the
 * older, still-live half of the contract: renderer state the menu's
 * rows show or change, and the dk_ui_request_* door through which
 * renderer-owned changes cross from the menu to the renderer.
 *
 * The drawing primitives this header used to declare (rectangles,
 * the baked-font atlas, the panel/dim/shadow assets) were retired
 * with the hand-drawn front-end; the renderer's 5x7 overlay text
 * (the FPS counter, the loading screen) is internal to nv2a_dk.c
 * and was never part of this contract.
 */
#ifndef NV2A_DK_UI_H
#define NV2A_DK_UI_H

/* The framebuffer size the menu lays itself out against. */
int  dk_ui_fb_w(void);
int  dk_ui_fb_h(void);

/* Renderer state the menu shows or changes. */
int         dk_ui_scale_choice(void);  /* 0 AUTO, 1 MAX, 2 1X, 3 1.5X, 4 2X, 5 3X, 6 4X */
float       dk_ui_scale_fixed(void);   /* the fixed scale, or -1 if not fixed */
const char *dk_ui_scale_name(void);    /* the mode's own words */
int         dk_ui_fps_state(void);     /* the corner counter is on */
int         dk_ui_fps_ms_state(void);  /* the counter's frame-time extra is on */
int         dk_ui_fps_drw_state(void); /* the counter's draws extra is on */
int         dk_ui_aniso_index(void);   /* 0 off, 1 2x, 2 4x, 3 8x, 4 16x */
int         dk_ui_stretch(void);       /* the presenter fills the screen */
void        dk_ui_set_stretch(int on);
int         dk_ui_post_crt(void);      /* the post pass's CRT step, 0..4 */
void        dk_ui_set_post_crt(int step);
int         dk_ui_post_vig(void);      /* the post pass's vignette step, 0..4 */
void        dk_ui_set_post_vig(int step);
int         dk_ui_post_aa(void);       /* the post pass's anti-aliasing switch, 0..1 */
void        dk_ui_set_post_aa(int step);
int         dk_ui_post_sharp(void);    /* the post pass's sharpen step, 0..3 */
void        dk_ui_set_post_sharp(int step);
unsigned    dk_ui_frames(void);        /* presented frames since boot */
unsigned    dk_ui_draws(void);         /* draws recorded since boot */
const char *dk_ui_data_dir(void);      /* the port's folder on the card */

/* Requests from the menu; applied by the renderer at the next
 * present. Music goes through the same door: the mix is the audio
 * engine's state, and the renderer thread is where the old menu's
 * requests were applied. */
void dk_ui_request_scale(int choice);
void dk_ui_request_fps(int on);
void dk_ui_request_fps_ms(int on);
void dk_ui_request_fps_drw(int on);
void dk_ui_request_music(int mix);
void dk_ui_request_fps_cap(int fps);   /* frame-rate cap; 0 = unlimited */

/* Live diagnostic state, for the Developer page's live-compare rows:
 * the getters read the renderer's own variables (the ones the read
 * sites consult every frame or every draw), and the requests cross
 * through the same apply latch as the scale and the frame-rate cap,
 * so a flip lands at the next present. The alpha-test pair's readers
 * latch inside the draw path, so their requests are variables the
 * read sites consult on every call instead. */
int  dk_ui_gpu_slow_ms(void);   /* the live threshold; -1 until latched */
void dk_ui_request_gpu_slow_ms(int ms);
int  dk_ui_hitch_ms(void);      /* the live threshold; 0 until latched */
void dk_ui_request_hitch_ms(int ms);
int  dk_ui_tex_every(void);
void dk_ui_request_tex_every(int n);
int  dk_ui_nodraw(void);
void dk_ui_request_nodraw(int on);
int  dk_ui_trace(void);
void dk_ui_request_trace(int on);
int  dk_ui_at_prove(void);
void dk_ui_request_at_prove(int on);
int  dk_ui_ez_test(void);
void dk_ui_request_ez_test(int on);

#endif /* NV2A_DK_UI_H */
