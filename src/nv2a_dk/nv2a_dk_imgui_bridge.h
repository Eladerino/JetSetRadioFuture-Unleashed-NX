/* nv2a_dk_imgui_bridge.h -- the renderer's services for the settings
 * menu's ImGui backend (nv2a_dk_imgui_dk.cpp), implemented in
 * nv2a_dk.c next to the machinery they wrap.
 *
 * The menu's front-end is Dear ImGui now, rendered through the 3D
 * pipeline from present() (the SwitchWakerHD overlay's shape). These
 * are the only doors the backend needs into the renderer: the device,
 * the frame's command buffer, the per-frame data ring (its stream
 * allocator), memblocks, and a shader loader with a small code block
 * of its own -- the game pack's code memory is sized for the pack
 * and stays untouched. */
#ifndef NV2A_DK_IMGUI_BRIDGE_H
#define NV2A_DK_IMGUI_BRIDGE_H

#include <deko3d.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

DkDevice   dk_ui_device(void);
DkCmdBuf   dk_ui_cmdbuf(void);
/* Bytes of this frame's data ring (data_alloc in nv2a_dk.c): CPU
 * pointer returned, GPU address in *gpu. Allocate a draw's data in
 * one call, record the draw in the same present. */
uint8_t   *dk_ui_data_alloc(uint32_t size, uint32_t align, DkGpuAddr *gpu);
DkMemBlock dk_ui_block(uint32_t size, uint32_t flags);
/* Load one DKSH module (from uam_compile_dksh) as a shader in the
 * backend's own code block. The blob must outlive the call: the
 * shader maker keeps its control section. */
int        dk_ui_shader_load(DkShader *sh, const uint8_t *dksh);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_DK_IMGUI_BRIDGE_H */
