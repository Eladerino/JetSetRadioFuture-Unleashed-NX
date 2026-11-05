// The settings menu's game-assets skin -- loader and draw side.
//
// What the approved mock-2 direction (Stage-0 mocks, Nov 5) is made
// of, all of it decoded at runtime from the player's own copy of
// the game, from sdmc:/switch/jsrf-unleashed/game/Media/:
//
//   * the JSRF pentagon badge ......... Title/TitleE.dat, child 3
//   * the UI font's Latin page ........ Font/jetfont.dat, child 4
//   * the message-window panel ........ Disp/SprNorm.dat, 512 sheet
//   * the TV-static patch ............. Disp/SprNorm.dat, 512 sheet
//   * the spray can ................... Disp/SprNorm.dat, 256 sheet
//   * the A / B / L / R button tiles .. Disp/SprNorm.dat, 512 sheet
//
// Container grammar (MULT/NORM chunks with interleaved 16-byte
// records, then 32-byte texture headers; format 5 = DXT1, 6 = DXT3)
// is the one pinned in the feasibility study of Nov 5 and proven
// against the JSRF ModTool's format notes; it is re-implemented
// here from that documentation. The DXT decoder below mirrors the
// study's Python decoder (mocks/tools/decode.py) in integer math,
// and a host build of this file (SKIN_HOST_TEST) reproduces its
// output byte for byte.
//
// Fallback discipline: any failure -- a file missing, a container
// that does not walk, a texture header that does not validate, a
// font page whose measured ink grid is not exactly the baked one
// -- leaves the skin off, and the menu renders exactly as shipped.
// Textures register once, at menu init, and live for the process;
// nothing here runs per frame except the draws themselves.
//
// Licence note: the art is the player's own game data, read at
// runtime; no game-derived bytes are in this file beyond measured
// rectangles and the font page's ink-run table (layout facts about
// the files, in the same spirit as a format header).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef SKIN_HOST_TEST
#include "imgui.h"
#include "imgui_internal.h"  // RegisterUserTexture (1.92.9b)
#include "nv2a_dk_menu_skin.h"
#endif

namespace {

/* ── file IO ─────────────────────────────────────────────────── */

uint8_t *read_file(const char *path, size_t &out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return nullptr; }
    uint8_t *buf = (uint8_t *)malloc(size_t(n));
    if (!buf) { fclose(f); return nullptr; }
    if (fread(buf, 1, size_t(n), f) != size_t(n)) { free(buf); fclose(f); return nullptr; }
    fclose(f);
    out_len = size_t(n);
    return buf;
}

/* ── the container walk (mirrors the study's decode.py) ──────── */

struct Leaf {
    char path[64];
    uint32_t off, size;
};

uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

void add_leaf(Leaf *leaves, int &n, int cap, const char *path, uint32_t off, uint32_t size)
{
    if (n >= cap) return;
    snprintf(leaves[n].path, sizeof leaves[n].path, "%s", path);
    leaves[n].off = off;
    leaves[n].size = size;
    n++;
}

void walk_rec(const uint8_t *buf, size_t len, uint32_t base, uint32_t size,
              const char *path, Leaf *leaves, int &n, int cap)
{
    if (size < 16 || base + size > len || (memcmp(buf + base, "MULT", 4) != 0 && memcmp(buf + base, "NORM", 4) != 0)) {
        add_leaf(leaves, n, cap, path, base, size);
        return;
    }
    uint32_t count = rd32(buf + base + 4);
    if (count > 4096) { add_leaf(leaves, n, cap, path, base, size); return; }
    uint32_t pos = base + 16;
    uint32_t child_off[512];
    uint32_t child_size[512];
    if (count > 512) { add_leaf(leaves, n, cap, path, base, size); return; }
    for (uint32_t i = 0; i < count; i++) {
        if (pos + 16 > base + size) { add_leaf(leaves, n, cap, path, base, size); return; }
        uint32_t bsize = rd32(buf + pos + 8);
        if (bsize > size || pos + 16 + bsize > base + size + 16) { add_leaf(leaves, n, cap, path, base, size); return; }
        child_off[i] = pos + 16;
        child_size[i] = bsize;
        pos += 16 + bsize;
    }
    for (uint32_t i = 0; i < count; i++) {
        char child_path[64];
        snprintf(child_path, sizeof child_path, "%s/%u", path, i);
        walk_rec(buf, len, child_off[i], child_size[i], child_path, leaves, n, cap);
    }
}

struct TexInfo {
    int res, fmt, alpha, swizzled, mips;
    uint32_t payload;  // level 0 + mip chain, in bytes
};

uint32_t mip_chain_bytes(int res, int fmt, int mips)
{
    uint32_t total = 0;
    for (int l = 0; l <= mips; l++) {
        int r = res >> l;
        if (r < 1) r = 1;
        int b = (r + 3) / 4;
        if (b < 1) b = 1;
        total += uint32_t(b * b) * (fmt == 5 ? 8u : 16u);
    }
    return total;
}

bool tex_header(const uint8_t *buf, size_t len, uint32_t off, TexInfo &t)
{
    if (off + 32 > len) return false;
    int res = int(int32_t(rd32(buf + off + 20)));
    int fmt = buf[off + 24];
    if (fmt != 5 && fmt != 6) return false;
    if (res <= 0 || res > 4096 || (res & (res - 1)) != 0) return false;
    int mips = int(int16_t(buf[off + 28] | (buf[off + 29] << 8)));
    if (mips < 0 || mips > 12) return false;
    t.res = res;
    t.fmt = fmt;
    t.alpha = buf[off + 25];
    t.swizzled = buf[off + 26];
    t.mips = mips;
    t.payload = mip_chain_bytes(res, fmt, mips);
    return true;
}

/* Find the leaf at `path` and validate it as a texture of the
 * expected shape; returns the payload offset (header skipped). */
bool find_texture(const uint8_t *buf, size_t len, const char *path, int want_res, int want_fmt,
                  const uint8_t *&payload)
{
    Leaf leaves[1024];
    int n = 0;
    walk_rec(buf, len, 0, uint32_t(len), "root", leaves, n, 1024);
    for (int i = 0; i < n; i++) {
        if (strcmp(leaves[i].path, path) != 0) continue;
        TexInfo t;
        if (!tex_header(buf, len, leaves[i].off, t)) return false;
        if (t.res != want_res || t.fmt != want_fmt || t.swizzled != 0) return false;
        if (32 + t.payload > leaves[i].size) return false;
        payload = buf + leaves[i].off + 32;
        return true;
    }
    return false;
}

/* ── DXT decode (integer math mirrors the study decoder) ─────── */

void expand565(uint16_t c, uint8_t *rgb)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    rgb[0] = uint8_t((r << 3) | (r >> 2));
    rgb[1] = uint8_t((g << 2) | (g >> 4));
    rgb[2] = uint8_t((b << 3) | (b >> 2));
}

/* Decode level 0 of a square DXT1/DXT3 texture to RGBA8. */
void decode_dxt(const uint8_t *payload, int res, int fmt, uint8_t *rgba)
{
    const int nb = res / 4;  // blocks per row
    for (int by = 0; by < nb; by++) {
        for (int bx = 0; bx < nb; bx++) {
            const uint8_t *blk = payload + (by * nb + bx) * (fmt == 5 ? 8 : 16);
            const uint8_t *col = fmt == 5 ? blk : blk + 8;
            uint16_t c0 = uint16_t(col[0] | (col[1] << 8));
            uint16_t c1 = uint16_t(col[2] | (col[3] << 8));
            uint32_t idx = uint32_t(col[4]) | (uint32_t(col[5]) << 8) | (uint32_t(col[6]) << 16) | (uint32_t(col[7]) << 24);
            uint8_t e0[3], e1[3], pal[4][3];
            expand565(c0, e0);
            expand565(c1, e1);
            memcpy(pal[0], e0, 3);
            memcpy(pal[1], e1, 3);
            if (fmt == 6 || c0 > c1) {
                for (int k = 0; k < 3; k++) {
                    pal[2][k] = uint8_t((2 * e0[k] + e1[k]) / 3);
                    pal[3][k] = uint8_t((e0[k] + 2 * e1[k]) / 3);
                }
            } else {
                for (int k = 0; k < 3; k++) pal[2][k] = uint8_t((e0[k] + e1[k]) / 2);
                pal[3][0] = pal[3][1] = pal[3][2] = 0;
            }
            for (int t = 0; t < 16; t++) {
                int sel = (idx >> (2 * t)) & 3;
                int x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
                uint8_t *px = rgba + (size_t(y) * res + x) * 4;
                px[0] = pal[sel][0];
                px[1] = pal[sel][1];
                px[2] = pal[sel][2];
                if (fmt == 6) {
                    uint8_t abyte = blk[t >> 1];
                    int nib = (t & 1) ? (abyte >> 4) : (abyte & 0xF);
                    px[3] = uint8_t(nib * 17);
                } else {
                    px[3] = (sel == 3 && c0 <= c1) ? 0 : 255;
                }
            }
        }
    }
}

/* ── the font page's ink grid (measured on the file; the loader
 * re-measures and refuses the skin if the page is not this one) ── */

struct SkinGlyph { int band, x0, x1; };
// [glyph table + band table are appended below this marker by the build]
static const SkinGlyph kGlyphs[84] = {
    { 0, 7, 14 },  // 0x21 '!'
    { 0, 27, 35 },  // 0x22 '"'
    { 0, 46, 59 },  // 0x23 '#'
    { 0, 66, 81 },  // 0x24 '$'
    { 0, 87, 101 },  // 0x25 '%'
    { 0, 107, 121 },  // 0x26 '&'
    { 0, 132, 136 },  // 0x27 "'"
    { 0, 158, 165 },  // 0x28 '('
    { 0, 171, 178 },  // 0x29 ')'
    { 0, 194, 204 },  // 0x2A '*'
    { 0, 214, 226 },  // 0x2B '+'
    { 0, 234, 238 },  // 0x2C ','
    { 1, 4, 16 },  // 0x2D '-'
    { 1, 24, 28 },  // 0x2E '.'
    { 1, 44, 61 },  // 0x2F '/'
    { 1, 65, 81 },  // 0x30 '0'
    { 1, 92, 99 },  // 0x31 '1'
    { 1, 107, 123 },  // 0x32 '2'
    { 1, 128, 144 },  // 0x33 '3'
    { 1, 151, 164 },  // 0x34 '4'
    { 1, 170, 186 },  // 0x35 '5'
    { 1, 191, 207 },  // 0x36 '6'
    { 1, 215, 228 },  // 0x37 '7'
    { 1, 233, 249 },  // 0x38 '8'
    { 2, 2, 18 },  // 0x39 '9'
    { 2, 28, 33 },  // 0x3A ':'
    { 2, 49, 55 },  // 0x3B ';'
    { 2, 67, 79 },  // 0x3C '<'
    { 2, 87, 100 },  // 0x3D '='
    { 2, 108, 119 },  // 0x3E '>'
    { 2, 131, 143 },  // 0x3F '?'
    { 2, 148, 165 },  // 0x40 '@'
    { 2, 170, 185 },  // 0x41 'A'
    { 2, 191, 207 },  // 0x42 'B'
    { 2, 211, 227 },  // 0x43 'C'
    { 2, 232, 248 },  // 0x44 'D'
    { 3, 2, 18 },  // 0x45 'E'
    { 3, 24, 39 },  // 0x46 'F'
    { 3, 44, 60 },  // 0x47 'G'
    { 3, 65, 81 },  // 0x48 'H'
    { 3, 91, 98 },  // 0x49 'I'
    { 3, 107, 123 },  // 0x4A 'J'
    { 3, 129, 145 },  // 0x4B 'K'
    { 3, 150, 162 },  // 0x4C 'L'
    { 3, 170, 187 },  // 0x4D 'M'
    { 3, 191, 207 },  // 0x4E 'N'
    { 3, 212, 228 },  // 0x4F 'O'
    { 3, 233, 249 },  // 0x50 'P'
    { 4, 2, 18 },  // 0x51 'Q'
    { 4, 23, 39 },  // 0x52 'R'
    { 4, 44, 60 },  // 0x53 'S'
    { 4, 69, 81 },  // 0x54 'T'
    { 4, 86, 102 },  // 0x55 'U'
    { 4, 111, 124 },  // 0x56 'V'
    { 4, 128, 145 },  // 0x57 'W'
    { 4, 149, 165 },  // 0x58 'X'
    { 4, 173, 187 },  // 0x59 'Y'
    { 4, 191, 207 },  // 0x5A 'Z'
    { 4, 219, 228 },  // 0x5B '['
    { 4, 233, 242 },  // 0x5C '\\'
    { 5, 9, 14 },  // 0x5D ']'
    { 5, 22, 37 },  // 0x5E '^'
    { 5, 52, 56 },  // 0x5F '_'
    { 5, 65, 79 },  // 0x60 '`'
    { 5, 86, 100 },  // 0x61 'a'
    { 5, 107, 121 },  // 0x62 'b'
    { 5, 128, 143 },  // 0x63 'c'
    { 5, 149, 163 },  // 0x64 'd'
    { 5, 173, 185 },  // 0x65 'e'
    { 5, 191, 205 },  // 0x66 'f'
    { 5, 212, 226 },  // 0x67 'g'
    { 5, 237, 244 },  // 0x68 'h'
    { 6, 4, 14 },  // 0x69 'i'
    { 6, 23, 37 },  // 0x6A 'j'
    { 6, 48, 55 },  // 0x6B 'k'
    { 6, 64, 80 },  // 0x6C 'l'
    { 6, 86, 100 },  // 0x6D 'm'
    { 6, 107, 121 },  // 0x6E 'n'
    { 6, 128, 142 },  // 0x6F 'o'
    { 6, 149, 163 },  // 0x70 'p'
    { 6, 171, 184 },  // 0x71 'q'
    { 6, 191, 205 },  // 0x72 'r'
    { 6, 214, 226 },  // 0x73 's'
    { 6, 233, 247 },  // 0x74 't'
};
static const int kBandY[7][2] = { { 4, 33 }, { 39, 66 }, { 73, 100 }, { 107, 133 }, { 141, 169 }, { 173, 203 }, { 209, 236 } };

/* ── the atlas ───────────────────────────────────────────────── */

constexpr int kAtlasW = 1024, kAtlasH = 512;
struct Rect { int x, y, w, h; };
/* Fixed packing (atlas pixels). DK_SPR_CAN's size is measured at
 * load from the keyed crop; its origin is fixed. The can's crop is
 * 34 px wide at x=354, so its ink can never pass x=388; the tiles
 * start at 390, clear of it. (Build H: the tiles used to start at
 * 382, and the A tile's blit -- packed after the can -- overwrote
 * the can's rightmost ink columns with its lime green, which drew
 * as a bright green bar beside every can. The load path checks
 * the clearance and refuses the skin rather than bake a sprite
 * with a neighbour's pixels in it.) */
Rect g_spr[9 /* DK_SPR_COUNT */] = {
    {0, 0, 79, 64},      // BADGE
    {80, 0, 188, 49},    // PANEL
    {120, 11, 120, 26},  // PANEL_IN (inside PANEL's rect above)
    {270, 0, 82, 67},    // STATIC
    {354, 0, 0, 0},      // CAN (filled at load)
    {390, 0, 30, 30},    // TILE_A
    {422, 0, 30, 30},    // TILE_B
    {454, 0, 30, 30},    // TILE_L
    {486, 0, 30, 30},    // TILE_R
};

uint8_t *g_atlas = nullptr;  // kAtlasW x kAtlasH RGBA, until registered
uint8_t *g_font = nullptr;   // 256 x 256 RGBA, until registered
bool g_skin_ok = false;

void blit(uint8_t *dst, int dst_w, int dx, int dy,
          const uint8_t *src, int src_w, int sx, int sy, int w, int h)
{
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t(dy + y) * dst_w + dx) * 4,
               src + (size_t(sy + y) * src_w + sx) * 4, size_t(w) * 4);
}

void force_opaque(uint8_t *dst, int dst_w, Rect r)
{
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            dst[(size_t(y) * dst_w + x) * 4 + 3] = 255;
}

/* The spray can sits on a white ground in its sheet; key the
 * ground out by flooding from the crop's border through white
 * pixels (the mocks' method), then take the ink's bounding box.
 * Crop is 34x58 at sheet (172,176); returns false if no ink. */
bool pack_can(const uint8_t *sheet256, Rect &out)
{
    constexpr int CX = 172, CY = 176, CW = 34, CH = 58;
    uint8_t crop[CW * CH * 4];
    for (int y = 0; y < CH; y++)
        memcpy(crop + size_t(y) * CW * 4, sheet256 + (size_t(CY + y) * 256 + CX) * 4, CW * 4);
    auto is_white = [&](int x, int y) {
        const uint8_t *p = crop + (size_t(y) * CW + x) * 4;
        return p[0] > 235 && p[1] > 235 && p[2] > 235 && p[3] > 0;
    };
    uint8_t seen[CW * CH] = {};
    int stack[CW * CH], sp = 0;
    for (int x = 0; x < CW; x++) { stack[sp++] = x; stack[sp++] = (CH - 1) * CW + x; }
    for (int y = 0; y < CH; y++) { stack[sp++] = y * CW; stack[sp++] = y * CW + (CW - 1); }
    while (sp > 0) {
        int cur = stack[--sp];
        int x = cur % CW, y = cur / CW;
        if (seen[cur] || !is_white(x, y)) continue;
        seen[cur] = 1;
        if (x > 0) stack[sp++] = cur - 1;
        if (x + 1 < CW) stack[sp++] = cur + 1;
        if (y > 0) stack[sp++] = cur - CW;
        if (y + 1 < CH) stack[sp++] = cur + CW;
    }
    for (int i = 0; i < CW * CH; i++)
        if (seen[i]) crop[size_t(i) * 4 + 3] = 0;
    int x0 = CW, y0 = CH, x1 = -1, y1 = -1;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            if (crop[(size_t(y) * CW + x) * 4 + 3] > 0) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) return false;
    out.w = x1 - x0 + 1;
    out.h = y1 - y0 + 1;
    for (int y = 0; y < out.h; y++)
        memcpy(g_atlas + (size_t(out.y + y) * kAtlasW + out.x) * 4,
               crop + (size_t(y0 + y) * CW + x0) * 4, size_t(out.w) * 4);
    return true;
}

/* Re-measure the font page's ink grid and compare it, exactly,
 * with the baked table. The baked table is only valid for the one
 * page it was measured on. */
bool font_grid_matches(const uint8_t *font)
{
    auto ink = [&](int x, int y) { return font[(size_t(y) * 256 + x) * 4 + 3] > 40; };
    int bands[16][2], nb = 0, start = -1;
    for (int y = 0; y < 256; y++) {
        bool row = false;
        for (int x = 0; x < 256 && !row; x++) row = ink(x, y);
        if (row && start < 0) start = y;
        if (!row && start >= 0) {
            if (nb >= 16) return false;
            bands[nb][0] = start;
            bands[nb][1] = y - 1;
            nb++;
            start = -1;
        }
    }
    if (start >= 0) {
        if (nb >= 16) return false;
        bands[nb][0] = start;
        bands[nb][1] = 255;
        nb++;
    }
    if (nb != 7) return false;
    for (int b = 0; b < 7; b++)
        if (bands[b][0] != kBandY[b][0] || bands[b][1] != kBandY[b][1]) return false;
    for (int b = 0; b < 7; b++) {
        int runs[16][2], nr = 0, s = -1;
        for (int x = 0; x < 256; x++) {
            bool col = false;
            for (int y = bands[b][0]; y <= bands[b][1] && !col; y++) col = ink(x, y);
            if (col && s < 0) s = x;
            if (!col && s >= 0) {
                if (nr >= 16) return false;
                runs[nr][0] = s;
                runs[nr][1] = x - 1;
                nr++;
                s = -1;
            }
        }
        if (s >= 0) {
            if (nr >= 16) return false;
            runs[nr][0] = s;
            runs[nr][1] = 255;
            nr++;
        }
        if (nr != 12) return false;
        for (int k = 0; k < 12; k++) {
            const SkinGlyph &g = kGlyphs[b * 12 + k];
            if (g.band != b || g.x0 != runs[k][0] || g.x1 != runs[k][1]) return false;
        }
    }
    return true;
}

/* The whole load: files -> sheets -> atlas + font page.
 * media_root ends in '/' (target) or is a host path (test). */
bool load_core(const char *media_root)
{
    char path[512];
    size_t n_font = 0, n_title = 0, n_spr = 0;
    snprintf(path, sizeof path, "%sFont/jetfont.dat", media_root);
    uint8_t *f_font = read_file(path, n_font);
    snprintf(path, sizeof path, "%sTitle/TitleE.dat", media_root);
    uint8_t *f_title = read_file(path, n_title);
    snprintf(path, sizeof path, "%sDisp/SprNorm.dat", media_root);
    uint8_t *f_spr = read_file(path, n_spr);
    bool ok = false;
    uint8_t *font = nullptr, *title = nullptr, *spr256 = nullptr, *spr512 = nullptr;
    const uint8_t *pay = nullptr;
    if (!f_font || !f_title || !f_spr) {
        fprintf(stderr, "[MENU] skin: a game file is missing; the skin stays off\n");
        goto out;
    }
    font = (uint8_t *)malloc(256 * 256 * 4);
    title = (uint8_t *)malloc(256 * 256 * 4);
    spr256 = (uint8_t *)malloc(256 * 256 * 4);
    spr512 = (uint8_t *)malloc(512 * 512 * 4);
    g_atlas = (uint8_t *)calloc(size_t(kAtlasW) * kAtlasH, 4);
    if (!font || !title || !spr256 || !spr512 || !g_atlas) goto out;
    if (!find_texture(f_font, n_font, "root/4", 256, 6, pay)) {
        fprintf(stderr, "[MENU] skin: jetfont's Latin page not found as expected; off\n");
        goto out;
    }
    decode_dxt(pay, 256, 6, font);
    if (!font_grid_matches(font)) {
        fprintf(stderr, "[MENU] skin: the font page's ink grid differs; off\n");
        goto out;
    }
    if (!find_texture(f_title, n_title, "root/0/3", 256, 6, pay)) {
        fprintf(stderr, "[MENU] skin: TitleE's badge sheet not found as expected; off\n");
        goto out;
    }
    decode_dxt(pay, 256, 6, title);
    if (!find_texture(f_spr, n_spr, "root/0/0", 256, 6, pay)) {
        fprintf(stderr, "[MENU] skin: SprNorm's 256 sheet not found as expected; off\n");
        goto out;
    }
    decode_dxt(pay, 256, 6, spr256);
    if (!find_texture(f_spr, n_spr, "root/0/1", 512, 6, pay)) {
        fprintf(stderr, "[MENU] skin: SprNorm's 512 sheet not found as expected; off\n");
        goto out;
    }
    decode_dxt(pay, 512, 6, spr512);
    /* Pack. Sprite sources are sheet rectangles measured on the
     * decoded sheets (the Stage-0 mocks' measurements). */
    blit(g_atlas, kAtlasW, 0, 0, title, 256, 3, 96, 79, 64);          // badge
    blit(g_atlas, kAtlasW, 80, 0, spr512, 512, 160, 185, 188, 49);    // panel
    blit(g_atlas, kAtlasW, 270, 0, spr512, 512, 250, 295, 82, 67);    // static
    /* The mocks use the static patch by luminance, lifted
     * (0.62 + 0.55 * lum); bake that lift in, grey, so a plain
     * tint multiply at draw time lands where the mock did. */
    for (int y = 0; y < 67; y++) {
        for (int x = 0; x < 82; x++) {
            uint8_t *p = g_atlas + (size_t(y) * kAtlasW + 270 + x) * 4;
            float lum = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
            float v = 0.62f + 0.55f * lum;
            if (v > 1.0f) v = 1.0f;
            p[0] = p[1] = p[2] = uint8_t(v * 255.0f);
        }
    }
    if (!pack_can(spr256, g_spr[4 /* DK_SPR_CAN */])) {
        fprintf(stderr, "[MENU] skin: the spray can did not key out; off\n");
        goto out;
    }
    /* The tiles pack after the can; if a future sheet's can ink
     * ever reached a tile's slot, the tile would bake into the
     * can (the build-H green bar). Fail closed instead. */
    if (g_spr[4].x + g_spr[4].w > g_spr[5 /* DK_SPR_TILE_A */].x) {
        fprintf(stderr, "[MENU] skin: the spray can's ink reaches the button tiles; off\n");
        goto out;
    }
    blit(g_atlas, kAtlasW, 390, 0, spr512, 512, 4, 461, 30, 30);      // A
    blit(g_atlas, kAtlasW, 422, 0, spr512, 512, 34, 461, 30, 30);     // B
    blit(g_atlas, kAtlasW, 454, 0, spr512, 512, 203, 240, 30, 30);    // L
    blit(g_atlas, kAtlasW, 486, 0, spr512, 512, 242, 240, 30, 30);    // R
    /* The panel, the static and the tiles are opaque UI art; the
     * mocks use them as RGB. The badge and the can keep their
     * alpha (a cut-out sticker, a keyed icon). */
    force_opaque(g_atlas, kAtlasW, g_spr[1]);
    force_opaque(g_atlas, kAtlasW, g_spr[3]);
    force_opaque(g_atlas, kAtlasW, g_spr[5]);
    force_opaque(g_atlas, kAtlasW, g_spr[6]);
    force_opaque(g_atlas, kAtlasW, g_spr[7]);
    force_opaque(g_atlas, kAtlasW, g_spr[8]);
    g_font = font;
    font = nullptr;
    ok = true;
    fprintf(stderr, "[MENU] skin: game art loaded (can %dx%d)\n", g_spr[4].w, g_spr[4].h);
out:
    free(f_font);
    free(f_title);
    free(f_spr);
    free(font);
    free(title);
    free(spr256);
    free(spr512);
    if (!ok) {
        free(g_atlas);
        g_atlas = nullptr;
    }
    return ok;
}

}  // namespace

#ifndef SKIN_HOST_TEST

namespace {

ImTextureData *g_atlas_tex = nullptr;
ImTextureData *g_font_tex = nullptr;

ImTextureData *register_rgba(const uint8_t *rgba, int w, int h)
{
    ImTextureData *td = IM_NEW(ImTextureData)();
    td->Create(ImTextureFormat_RGBA32, w, h);
    memcpy(td->Pixels, rgba, size_t(w) * size_t(h) * 4);
    td->SetStatus(ImTextureStatus_WantCreate);
    ImGui::RegisterUserTexture(td);
    return td;
}

}  // namespace

bool dk_menu_skin_init(void)
{
    if (g_skin_ok) return true;
    if (!load_core("sdmc:/switch/jsrf-unleashed/game/Media/")) return false;
    g_atlas_tex = register_rgba(g_atlas, kAtlasW, kAtlasH);
    g_font_tex = register_rgba(g_font, 256, 256);
    free(g_atlas);
    g_atlas = nullptr;
    free(g_font);
    g_font = nullptr;
    if (!g_atlas_tex || !g_font_tex) return false;
    g_skin_ok = true;
    fprintf(stderr, "[MENU] skin: live (atlas + game font registered)\n");
    return true;
}

bool dk_menu_skin_ok(void) { return g_skin_ok; }

ImTextureRef dk_menu_skin_atlas_ref(void) { return g_atlas_tex->GetTexRef(); }

void dk_menu_skin_sprite_uv(DkSkinSprite s, ImVec2 &uv0, ImVec2 &uv1)
{
    const Rect &r = g_spr[s];
    uv0 = ImVec2(float(r.x) / kAtlasW, float(r.y) / kAtlasH);
    uv1 = ImVec2(float(r.x + r.w) / kAtlasW, float(r.y + r.h) / kAtlasH);
}

ImVec2 dk_menu_skin_sprite_px(DkSkinSprite s)
{
    const Rect &r = g_spr[s];
    return ImVec2(float(r.w), float(r.h));
}

void dk_menu_draw_panel_wash(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float alpha, float corner)
{
    if (!g_skin_ok) return;
    const Rect &r = g_spr[DK_SPR_PANEL];
    const float sc = 10.0f;  // the panel's border weight, in atlas pixels
    const float xs[4] = {0.0f, sc, float(r.w) - sc, float(r.w)};
    const float ys[4] = {0.0f, sc, float(r.h) - sc, float(r.h)};
    const float xd[4] = {p0.x, p0.x + corner, p1.x - corner, p1.x};
    const float yd[4] = {p0.y, p0.y + corner, p1.y - corner, p1.y};
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            dl->AddImage(g_atlas_tex->GetTexRef(),
                         ImVec2(xd[i], yd[j]), ImVec2(xd[i + 1], yd[j + 1]),
                         ImVec2(float(r.x + xs[i]) / kAtlasW, float(r.y + ys[j]) / kAtlasH),
                         ImVec2(float(r.x + xs[i + 1]) / kAtlasW, float(r.y + ys[j + 1]) / kAtlasH),
                         col);
}

namespace {
constexpr float kCell = 256.0f / 12.0f;   // the font page's grid pitch
constexpr float kBandNominal = 28.0f;     // scale reference for ink_h
}  // namespace

bool dk_menu_game_text_covered(const char *s)
{
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ') continue;
        if (c < 0x21 || c > 0x74) return false;
    }
    return true;
}

float dk_menu_game_text_width(const char *s, float ink_h)
{
    return float(strlen(s)) * kCell * (ink_h / kBandNominal);
}

float dk_menu_draw_game_text(ImDrawList *dl, ImVec2 pos, const char *s, float ink_h, ImU32 col)
{
    if (!g_skin_ok) return 0.0f;
    const float scale = ink_h / kBandNominal;
    float pen = 0.0f;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ') { pen += kCell * scale; continue; }
        if (c < 0x21 || c > 0x74) { pen += kCell * scale; continue; }
        const int idx = c - 0x21;
        const SkinGlyph &g = kGlyphs[idx];
        const int col12 = idx % 12;
        const float gx = pos.x + pen + (float(g.x0) - col12 * kCell) * scale;
        const float gw = float(g.x1 - g.x0 + 1) * scale;
        const float gh = float(kBandY[g.band][1] - kBandY[g.band][0] + 1) * scale;
        dl->AddImage(g_font_tex->GetTexRef(), ImVec2(gx, pos.y), ImVec2(gx + gw, pos.y + gh),
                     ImVec2(float(g.x0) / 256.0f, float(kBandY[g.band][0]) / 256.0f),
                     ImVec2(float(g.x1 + 1) / 256.0f, float(kBandY[g.band][1] + 1) / 256.0f),
                     col);
        pen += kCell * scale;
    }
    return pen;
}

#else  /* SKIN_HOST_TEST */

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: skin_test <Media root with trailing slash> <out prefix>\n");
        return 2;
    }
    if (!load_core(argv[1])) {
        fprintf(stderr, "load_core FAILED\n");
        return 1;
    }
    char path[512];
    snprintf(path, sizeof path, "%s.atlas.rgba", argv[2]);
    FILE *f = fopen(path, "wb");
    fwrite(g_atlas, 1, size_t(kAtlasW) * kAtlasH * 4, f);
    fclose(f);
    snprintf(path, sizeof path, "%s.font.rgba", argv[2]);
    f = fopen(path, "wb");
    fwrite(g_font, 1, 256 * 256 * 4, f);
    fclose(f);
    printf("can rect: %d,%d %dx%d\n", g_spr[4].x, g_spr[4].y, g_spr[4].w, g_spr[4].h);
    return 0;
}

#endif
