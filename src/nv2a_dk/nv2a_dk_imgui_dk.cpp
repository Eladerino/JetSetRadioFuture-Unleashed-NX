// The settings menu's Dear ImGui draw data, drawn by the deko3d
// renderer over the picture in the present pass.
//
// Adapted from SwitchWakerHD's runtime/src/gfx/deko/overlay_dk.cpp
// (centollOS, with thanks -- the same present slot, the same blend
// state, the same ImGui 1.92 texture protocol), re-plumbed to this
// renderer's services: the frame's command buffer and per-frame data
// ring come through nv2a_dk_imgui_bridge.h, textures live in their
// own memblocks with a private descriptor set (the game's sets are
// rebound by frame_state() every frame, so the overlay's bindings
// never leak into a batch), and the shader pair is compiled at start
// by uam -- the route every shader in this build takes -- instead of
// being embedded at build time. Everything is drawn with the 3D
// pipeline (dkCmdBufDrawIndexed); the 2D engine and its blit modes
// are not used here at all.

// Licence: MPL-2.0 — the licence of SwitchWakerHD, the project
// this file is adapted from (its debug-server core in this tree,
// src/nv2a_dk/debug_server.cpp/.h, states the same licence).
// Full text: LICENSES/MPL-2.0.txt.

#include "nv2a_dk_imgui_dk.h"
#include "nv2a_dk_imgui_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "uam.h"

namespace {

constexpr uint32_t kFirstSlot = 1, kSlots = 8;  // image descriptor slots for the overlay's textures
struct Texture {
    bool used = false;
    DkImage image;
    DkMemBlock mem = nullptr;
    uint64_t mem_size = 0;
};
Texture g_tex[kSlots];
DkMemBlock g_desc_mem = nullptr;
DkGpuAddr g_imgdesc_gpu = 0, g_sampdesc_gpu = 0;
DkShader g_vs, g_fs;
bool g_ready = false;

/* A destroyed texture's block is retired, not freed: the GPU may
 * still draw with it this frame, and the overlay creates a handful
 * of textures in its life (the font atlas, in practice one). */
DkMemBlock g_retired[16];
uint32_t g_retired_n = 0;

void retire_block(DkMemBlock mem)
{
    if (!mem) return;
    if (g_retired_n < sizeof g_retired / sizeof g_retired[0])
        g_retired[g_retired_n++] = mem;
    /* else: leaked deliberately, bounded by the slot count */
}

void destroy_texture(uint32_t slot)
{
    if (slot < kFirstSlot || slot >= kSlots || !g_tex[slot].used) return;
    retire_block(g_tex[slot].mem);
    g_tex[slot].mem = nullptr;
    g_tex[slot].mem_size = 0;
    g_tex[slot].used = false;
}

// ImGui 1.92 texture protocol: create, update (the whole texture) and
// destroy font atlas pages. The layout maker is used with its
// defaults, exactly as the renderer's own tex_storage makes game
// textures; no pitch is ever set by hand.
void update_texture(ImTextureData *tex)
{
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        destroy_texture(uint32_t(tex->GetTexID()));
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
        return;
    }
    if (tex->Status != ImTextureStatus_WantCreate && tex->Status != ImTextureStatus_WantUpdates) return;
    if (tex->Format != ImTextureFormat_RGBA32) {
        fprintf(stderr, "[MENU] imgui: unexpected texture format %d\n", (int)tex->Format);
        tex->SetStatus(ImTextureStatus_OK);
        return;
    }
    DkCmdBuf cmd = dk_ui_cmdbuf();
    uint32_t slot = uint32_t(tex->GetTexID());
    if (tex->Status == ImTextureStatus_WantCreate || slot < kFirstSlot || slot >= kSlots || !g_tex[slot].used) {
        destroy_texture(slot);
        slot = 0;
        for (uint32_t i = kFirstSlot; i < kSlots && !slot; i++)
            if (!g_tex[i].used) slot = i;
        if (!slot) {
            fprintf(stderr, "[MENU] imgui: no free texture slot for a %dx%d texture\n", tex->Width, tex->Height);
            tex->SetStatus(ImTextureStatus_OK);
            return;
        }
        Texture &t = g_tex[slot];
        DkImageLayoutMaker m;
        dkImageLayoutMakerDefaults(&m, dk_ui_device());
        m.format = DkImageFormat_RGBA8_Unorm;
        m.dimensions[0] = uint32_t(tex->Width);
        m.dimensions[1] = uint32_t(tex->Height);
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &m);
        uint32_t align = dkImageLayoutGetAlignment(&layout);
        if (align < DK_MEMBLOCK_ALIGNMENT) align = DK_MEMBLOCK_ALIGNMENT;
        uint64_t size = dkImageLayoutGetSize(&layout);
        if (!t.mem || t.mem_size < size + align) {
            retire_block(t.mem);
            t.mem = dk_ui_block(uint32_t(size) + align,
                                DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
            t.mem_size = t.mem ? size + align : 0;
            if (!t.mem) {
                fprintf(stderr, "[MENU] imgui: no memory for a %dx%d texture\n", tex->Width, tex->Height);
                tex->SetStatus(ImTextureStatus_OK);
                return;
            }
        }
        uint32_t off = uint32_t((align - (dkMemBlockGetGpuAddr(t.mem) % align)) % align);
        dkImageInitialize(&t.image, &layout, t.mem, off);
        t.used = true;
        DkImageView view;
        dkImageViewDefaults(&view, &t.image);
        DkImageDescriptor d;
        dkImageDescriptorInitialize(&d, &view, false, false);
        DkImageDescriptor *desc_cpu =
            (DkImageDescriptor *)dkMemBlockGetCpuAddr(g_desc_mem);
        desc_cpu[slot] = d;
        tex->SetTexID(ImTextureID(slot));
        fprintf(stderr, "[MENU] imgui texture %u: %dx%d\n", slot, tex->Width, tex->Height);
    }
    // the pixels through the frame's data ring, copied into the
    // image by the GPU
    const uint32_t bytes = uint32_t(tex->Width) * uint32_t(tex->Height) * 4;
    DkGpuAddr gpu = 0;
    uint8_t *staging = dk_ui_data_alloc(bytes, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT, &gpu);
    if (!staging) return;  // tried again next frame (the status stays)
    memcpy(staging, tex->GetPixels(), bytes);
    DkImageView view;
    dkImageViewDefaults(&view, &g_tex[slot].image);
    const DkCopyBuf src = {gpu, 0, 0};
    const DkImageRect rect = {0, 0, 0, uint32_t(tex->Width), uint32_t(tex->Height), 1};
    dkCmdBufCopyBufferToImage(cmd, &src, &view, &rect, 0);
    // the copy and the descriptor done before any draw samples the texture
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    tex->SetStatus(ImTextureStatus_OK);
}

/* SwitchWakerHD's shader pair, verbatim (runtime/src/gfx/deko/
 * shaders/imgui_vsh.glsl, imgui_fsh.glsl): ImGui's pixels to
 * normalized coordinates with y down; the vertex shader negates y
 * (deko3d's clip-space y is up). */
const char kImguiVsGlsl[] =
    "#version 460\n"
    "layout(location = 0) in vec2 inPos;\n"
    "layout(location = 1) in vec2 inUv;\n"
    "layout(location = 2) in vec4 inColor;\n"
    "layout(location = 0) out vec2 outUv;\n"
    "layout(location = 1) out vec4 outColor;\n"
    "layout(std140, binding = 0) uniform Xform {\n"
    "    vec4 xform;\n"
    "};\n"
    "void main() {\n"
    "    outUv = inUv;\n"
    "    outColor = inColor;\n"
    "    vec2 p = inPos * xform.xy + xform.zw;\n"
    "    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
    "}\n";
const char kImguiFsGlsl[] =
    "#version 460\n"
    "layout(location = 0) in vec2 inUv;\n"
    "layout(location = 1) in vec4 inColor;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "layout(binding = 0) uniform sampler2D image;\n"
    "void main() { outColor = inColor * texture(image, inUv); }\n";

}  // namespace

bool dk_imgui_backend_init(void)
{
    if (g_ready) return true;
    /* The shaders, compiled by uam like every shader in this build.
     * The DKSH blobs are kept for the process's life: a shader's
     * control section is read from them (the game pack does the
     * same with its buffer). */
    uint32_t vs_size = 0, fs_size = 0;
    const uint8_t *vs_dksh = (const uint8_t *)uam_compile_dksh(UAM_STAGE_VERTEX, kImguiVsGlsl, &vs_size);
    const uint8_t *fs_dksh = (const uint8_t *)uam_compile_dksh(UAM_STAGE_FRAGMENT, kImguiFsGlsl, &fs_size);
    if (!vs_dksh || !fs_dksh || !dk_ui_shader_load(&g_vs, vs_dksh) || !dk_ui_shader_load(&g_fs, fs_dksh)) {
        fprintf(stderr, "[MENU] imgui: shader build failed; the menu stays shut\n");
        return false;
    }
    /* The overlay's own descriptor set: kSlots image descriptors and
     * one sampler (linear, clamped -- the font atlas's sampler, slot
     * 0 of the set, as in SwitchWakerHD). CPU-uncached like the
     * renderer's descriptor block, so writes need no flush; a draw
     * that follows a write is fenced by the barrier in
     * update_texture. */
    g_desc_mem = dk_ui_block(kSlots * uint32_t(sizeof(DkImageDescriptor)) + uint32_t(sizeof(DkSamplerDescriptor)),
                             DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!g_desc_mem) {
        fprintf(stderr, "[MENU] imgui: no descriptor memory; the menu stays shut\n");
        return false;
    }
    memset(dkMemBlockGetCpuAddr(g_desc_mem), 0,
           kSlots * sizeof(DkImageDescriptor) + sizeof(DkSamplerDescriptor));
    g_imgdesc_gpu = dkMemBlockGetGpuAddr(g_desc_mem);
    g_sampdesc_gpu = g_imgdesc_gpu + kSlots * sizeof(DkImageDescriptor);
    DkSampler smp;
    dkSamplerDefaults(&smp);
    smp.magFilter = DkFilter_Linear;
    smp.minFilter = DkFilter_Linear;
    smp.mipFilter = DkMipFilter_None;
    smp.wrapMode[0] = DkWrapMode_ClampToEdge;
    smp.wrapMode[1] = DkWrapMode_ClampToEdge;
    smp.wrapMode[2] = DkWrapMode_ClampToEdge;
    DkSamplerDescriptor *samp_cpu =
        (DkSamplerDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(g_desc_mem) + kSlots * sizeof(DkImageDescriptor));
    dkSamplerDescriptorInitialize(samp_cpu, &smp);
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererName = "jsrf_deko3d";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    g_ready = true;
    fprintf(stderr, "[MENU] imgui: deko3d backend ready\n");
    return true;
}

// into the bound render target (the swapchain image), ww x wh pixels,
// top-left origin like ImGui
void dk_imgui_render(ImDrawData *d, int ww, int wh)
{
    if (!g_ready || !d || d->DisplaySize.x <= 0 || d->DisplaySize.y <= 0) return;
    DkCmdBuf cmd = dk_ui_cmdbuf();
    if (d->Textures)
        for (ImTextureData *tex : *d->Textures)
            if (tex->Status != ImTextureStatus_OK) update_texture(tex);
    if (d->TotalVtxCount <= 0) return;
    // all lists' vertices, then all indices, then the transform,
    // into the frame's data ring
    const uint32_t vBytes = uint32_t(d->TotalVtxCount) * sizeof(ImDrawVert);
    const uint32_t iBytes = uint32_t(d->TotalIdxCount) * sizeof(ImDrawIdx);
    DkGpuAddr vGpu = 0, iGpu = 0, uGpu = 0;
    uint8_t *vtx = dk_ui_data_alloc(vBytes, 16, &vGpu);
    uint8_t *idx = dk_ui_data_alloc(iBytes, 16, &iGpu);
    uint8_t *ubo = dk_ui_data_alloc(256, DK_UNIFORM_BUF_ALIGNMENT, &uGpu);
    if (!vtx || !idx || !ubo) return;
    size_t vOff = 0, iOff = 0;
    for (const ImDrawList *l : d->CmdLists) {
        memcpy(vtx + vOff, l->VtxBuffer.Data, size_t(l->VtxBuffer.Size) * sizeof(ImDrawVert));
        memcpy(idx + iOff, l->IdxBuffer.Data, size_t(l->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vOff += size_t(l->VtxBuffer.Size) * sizeof(ImDrawVert);
        iOff += size_t(l->IdxBuffer.Size) * sizeof(ImDrawIdx);
    }
    // ImGui's pixels to normalized coordinates with y down; the
    // vertex shader negates y (deko3d's clip-space y is up).
    // The scissor rects stay in window coordinates from the top left.
    const float xform[4] = {2.0f / d->DisplaySize.x, 2.0f / d->DisplaySize.y,
                            -1.0f - d->DisplayPos.x * 2.0f / d->DisplaySize.x,
                            -1.0f - d->DisplayPos.y * 2.0f / d->DisplaySize.y};
    memcpy(ubo, xform, sizeof xform);
    const DkShader *sh[] = {&g_vs, &g_fs};
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, sh, 2);
    dkCmdBufBindImageDescriptorSet(cmd, g_imgdesc_gpu, kSlots);
    dkCmdBufBindSamplerDescriptorSet(cmd, g_sampdesc_gpu, 1);
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cmd, &rs);
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkColorStateSetBlendEnable(&cs, 0, true);
    dkCmdBufBindColorState(cmd, &cs);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkCmdBufBindColorWriteState(cmd, &cw);
    DkBlendState bs;
    dkBlendStateDefaults(&bs);
    dkBlendStateSetFactors(&bs, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha,
                           DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
    dkCmdBufBindBlendStates(cmd, 0, &bs, 1);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = false;
    ds.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(cmd, &ds);
    const DkViewport vp = {0, 0, float(ww), float(wh), 0.0f, 1.0f};
    dkCmdBufSetViewports(cmd, 0, &vp, 1);
    const DkBufExtents u = {uGpu, 256};
    dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, &u, 1);
    static const DkVtxAttribState attribs[] = {
        DkVtxAttribState{0, 0, offsetof(ImDrawVert, pos), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(ImDrawVert, uv), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(ImDrawVert, col), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
    };
    static const DkVtxBufferState buffers[] = {DkVtxBufferState{sizeof(ImDrawVert), 0}};
    dkCmdBufBindVtxAttribState(cmd, attribs, 3);
    dkCmdBufBindVtxBufferState(cmd, buffers, 1);
    const DkBufExtents vb = {vGpu, vBytes};
    dkCmdBufBindVtxBuffers(cmd, 0, &vb, 1);
    dkCmdBufBindIdxBuffer(cmd, sizeof(ImDrawIdx) == 2 ? DkIdxFormat_Uint16 : DkIdxFormat_Uint32, iGpu);
    const float sx = ww / d->DisplaySize.x, sy = wh / d->DisplaySize.y;
    uint32_t bound = 0;
    int vtxBase = 0, idxBase = 0;
    for (const ImDrawList *l : d->CmdLists) {
        for (const ImDrawCmd &c : l->CmdBuffer) {
            if (c.UserCallback) continue;
            float x0 = (c.ClipRect.x - d->DisplayPos.x) * sx, y0 = (c.ClipRect.y - d->DisplayPos.y) * sy;
            float x1 = (c.ClipRect.z - d->DisplayPos.x) * sx, y1 = (c.ClipRect.w - d->DisplayPos.y) * sy;
            x0 = std::max(x0, 0.0f);
            y0 = std::max(y0, 0.0f);
            x1 = std::min(x1, float(ww));
            y1 = std::min(y1, float(wh));
            if (x1 <= x0 || y1 <= y0) continue;
            const uint32_t slot = uint32_t(c.GetTexID());
            if (slot < kFirstSlot || slot >= kSlots || !g_tex[slot].used) continue;
            if (slot != bound) {
                const DkResHandle h = dkMakeTextureHandle(slot, 0);
                dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &h, 1);
                bound = slot;
            }
            const DkScissor sc = {uint32_t(x0), uint32_t(y0), uint32_t(x1 - x0), uint32_t(y1 - y0)};
            dkCmdBufSetScissors(cmd, 0, &sc, 1);
            dkCmdBufDrawIndexed(cmd, DkPrimitive_Triangles, c.ElemCount, 1, c.IdxOffset + uint32_t(idxBase),
                                int32_t(c.VtxOffset) + vtxBase, 0);
        }
        vtxBase += l->VtxBuffer.Size;
        idxBase += l->IdxBuffer.Size;
    }
}
