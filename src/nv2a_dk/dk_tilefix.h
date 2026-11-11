/* dk_tilefix.h -- deko3d 0.5.0's block-linear tile-size bug, and the
 * workaround for it (jsrf-nx; SwitchWakerHD's FixTileSize, via mvp-nx's
 * dk_core.cpp).
 *
 * Before dkImageLayoutInitialize of a block-linear 2D image whose level
 * 0 is `rows` tall (compressed formats: rows of 4x4 blocks): deko3d
 * 0.5.0 gives images of 6 to 8 rows of 8-row blocks a tile height that
 * its layout and its descriptor disagree on, and the second column of
 * blocks of level 0 lands on mip 1. This sets the tile its level 0
 * really uses. Call it on the layout maker after type, format and
 * dimensions are set and before the layout is initialised.
 */
#ifndef DK_TILEFIX_H
#define DK_TILEFIX_H

#include <deko3d.h>
#include <stdint.h>

static inline void dk_fix_tile_size(DkImageLayoutMaker *m, uint32_t rows)
{
    uint32_t gobs, tile, shrunk;
    if (m->flags & (DkImageFlags_PitchLinear | DkImageFlags_CustomTileSize
                    | DkImageFlags_UsageVideo))
        return;
    if (m->type == DkImageType_1D || m->type == DkImageType_1DArray
        || m->type == DkImageType_3D || m->type == DkImageType_Buffer)
        return;  /* 3D: deko3d never shrinks the depth tile at level 0 */
    if ((m->type == DkImageType_2DMS || m->type == DkImageType_2DMSArray)
        && (m->msMode == DkMsMode_4x || m->msMode == DkMsMode_8x))
        rows *= 2;  /* the multisampled height */
    /* deko3d's choice (pickTileSize of 1.5 times the height in 8-row
     * GOBs) ... */
    gobs = (rows + rows / 2 + 7) / 8;
    tile = gobs >= 16 ? 4 : gobs >= 8 ? 3 : gobs >= 4 ? 2 : gobs >= 2 ? 1 : 0;
    /* ... and the shrink its layout applies to level 0 (calcLevelOffset),
     * which the descriptor and engines do not */
    shrunk = tile;
    while (shrunk && (8u << (shrunk - 1)) >= rows)
        --shrunk;
    if (shrunk == tile)
        return;
    m->flags |= DkImageFlags_CustomTileSize;
    m->tileSize = (DkTileSize)shrunk;
}

#endif
