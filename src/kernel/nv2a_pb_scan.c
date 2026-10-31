#include <stddef.h>
/*
 * Read-only survey of the pushbuffer a title submits.
 *
 * The title builds NV2A commands in guest RAM and advances DMA_PUT; nothing
 * here executes them, so the framebuffer stays black however far the game
 * gets. Before any of that can be made to draw, the question is what it
 * actually asks for -- which methods, on which object classes, how many of
 * them -- because that is the difference between "the existing PGRAPH
 * translator nearly covers this" and "this needs a real one".
 *
 * Purely a reader: it walks the buffer and counts, and never writes to guest
 * memory or to the GPU state. Enabled with RECOMP_PB_SCAN.
 *
 * Pushbuffer encoding (NV20/NV2A), one dword per command header:
 *   (w & 0xE0030003) == 0x00000000  increasing methods
 *   (w & 0xE0030003) == 0x40000000  non-increasing (same method, count params)
 *   (w & 0x00000003) == 0x00000001  jump
 *   (w & 0x00000003) == 0x00000002  call
 *   (w & 0xFFFF0003) == 0x00020000  return
 * For a method header: count = (w >> 18) & 0x7FF, subchannel = (w >> 13) & 7,
 * method = w & 0x1FFC.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define PB_MAX_METHODS 4096

static struct { uint32_t method, subch, count; } s_seen[PB_MAX_METHODS];
static int s_seen_count;

/* Parse health. An inventory is only worth reading if the walk stayed in step
 * with the command stream: a decoder that desynchronises produces plausible
 * looking method numbers out of parameter data, and the counts then describe
 * nothing. Unrecognised words are the tell. */
static uint32_t s_tot_words, s_tot_unknown, s_tot_jumps, s_tot_segments;

/* Executing is opt-in separately from surveying: a survey is read-only, while
 * the executor writes to guest memory. */
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern int nv2a_pb_exec_indices(uint32_t subch, uint32_t method, const uint32_t *w, uint32_t n);
extern void nv2a_pb_exec_report(void);
static int s_exec_enabled = -1;
static int s_scan_enabled = -1;

/* An index from (subchannel, method) straight to the inventory slot.
 *
 * This function used to walk the inventory looking for the pair, and it runs
 * on every word of the pushbuffer -- two million a second on this title, times
 * up to two hundred entries scanned. Measured on an M2 Pro, the pushbuffer
 * walk was 84% of the frame and this was most of it: an inventory kept for
 * diagnostics was costing more than the emulated GPU.
 *
 * A method is 13 bits and always 4-aligned, a subchannel is 3, so the pair
 * fits in a 14-bit index and the lookup is one load. Thirty-two kilobytes.
 */
#define PB_INDEX_BITS 14
static int16_t s_seen_index[1 << PB_INDEX_BITS];
static int     s_seen_index_ready;

static void note(uint32_t subch, uint32_t method)
{
    uint32_t key;

    if (!s_seen_index_ready) {
        memset(s_seen_index, 0xFF, sizeof s_seen_index);   /* -1 everywhere */
        s_seen_index_ready = 1;
    }
    key = ((subch & 7u) << 11) | ((method >> 2) & 0x7FFu);
    if (s_seen_index[key] >= 0) {
        s_seen[s_seen_index[key]].count++;
        return;
    }
    if (s_seen_count >= PB_MAX_METHODS) {
        /* Silently dropping past the cap is how a truncated inventory reads as
         * "the title never does that" -- exactly the wrong conclusion when the
         * inventory is being used to decide what to implement. */
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "[PB] method table full at %d -- inventory is"
                            " truncated\n", PB_MAX_METHODS);
        }
    }
    if (s_seen_count < PB_MAX_METHODS) {
        s_seen[s_seen_count].method = method;
        s_seen[s_seen_count].subch  = subch;
        s_seen[s_seen_count].count  = 1;
        s_seen_index[key] = (int16_t)s_seen_count;
        s_seen_count++;
    }
}

/* NV097 (Kelvin 3D class) methods worth naming. The point of the survey is to
 * decide what a translator has to implement, and a bare method number does not
 * answer that -- "0x1808 x412" only means something once it reads
 * INLINE_ARRAY. Unnamed ones still get counted. */
static const struct { uint32_t m; const char *name; } NV097_NAMES[] = {
    { 0x0000, "SET_OBJECT" },
    { 0x0100, "NO_OPERATION" },
    { 0x0104, "SET_WARNING_ENABLE" },
    { 0x0130, "SET_FLIP_READ" },
    { 0x0200, "SET_SURFACE_CLIP_HORIZONTAL" },
    { 0x0204, "SET_SURFACE_CLIP_VERTICAL" },
    { 0x0208, "SET_SURFACE_FORMAT" },
    { 0x020C, "SET_SURFACE_PITCH" },
    { 0x0210, "SET_SURFACE_COLOR_OFFSET" },
    { 0x0214, "SET_SURFACE_ZETA_OFFSET" },
    { 0x0300, "SET_ALPHA_TEST_ENABLE" },
    { 0x0304, "SET_BLEND_ENABLE" },
    { 0x030C, "SET_DEPTH_TEST_ENABLE" },
    { 0x0310, "SET_DITHER_ENABLE" },
    { 0x0314, "SET_LIGHTING_ENABLE" },
    { 0x033C, "SET_CULL_FACE_ENABLE" },
    { 0x0340, "SET_DEPTH_MASK" },
    { 0x0350, "SET_CLEAR_DEPTH_VALUE" },
    { 0x1D8C, "SET_CLEAR_DEPTH" },
    { 0x1D90, "SET_COLOR_CLEAR_VALUE" },
    { 0x1D94, "CLEAR_SURFACE" },
    { 0x1D6C, "SET_ZSTENCIL_CLEAR" },
    { 0x0B80, "SET_TRANSFORM_PROGRAM" },
    { 0x0B00, "SET_TRANSFORM_CONSTANT" },
    { 0x1720, "SET_VERTEX_DATA_ARRAY_OFFSET" },
    { 0x1760, "SET_VERTEX_DATA_ARRAY_FORMAT" },
    { 0x17FC, "SET_BEGIN_END" },
    { 0x1800, "ARRAY_ELEMENT16" },
    { 0x1808, "INLINE_ARRAY" },
    { 0x1810, "DRAW_ARRAYS" },
    { 0x1B00, "SET_TEXTURE_OFFSET" },
    { 0x1B04, "SET_TEXTURE_FORMAT" },
    { 0x1B08, "SET_TEXTURE_ADDRESS" },
    { 0x1B0C, "SET_TEXTURE_CONTROL0" },
    { 0x1B14, "SET_TEXTURE_IMAGE_RECT" },
    { 0x0FD8, "SET_COMBINER_*" },
    { 0x0000, NULL },
};

static const char *nv097_name(uint32_t m)
{
    int i;
    for (i = 0; NV097_NAMES[i].name; i++)
        if (NV097_NAMES[i].m == m)
            return NV097_NAMES[i].name;
    return "";
}

void nv2a_pb_scan_report(void)
{
    int i;

    if (s_exec_enabled > 0)
        nv2a_pb_exec_report();
    if (!s_seen_count || s_scan_enabled <= 0)
        return;
    fprintf(stderr, "[PB] %u segments, %u words, %u jumps, %u unrecognised"
                    " -- %d distinct (subchannel, method) pairs\n",
            s_tot_segments, s_tot_words, s_tot_jumps, s_tot_unknown,
            s_seen_count);
    for (i = 0; i < s_seen_count; i++)
        fprintf(stderr, "  [PB]   subch %u  method 0x%04X  x%-6u %s\n",
                s_seen[i].subch, s_seen[i].method, s_seen[i].count,
                nv097_name(s_seen[i].method));
    fflush(stderr);
}

/*
 * CALL and RETURN.
 *
 * A call word ((w & 3) == 2) runs the commands at its target until a RETURN
 * (0x00020000), then carries on after the call -- one level deep, as the
 * NV2A's single subroutine register allows. Xbox D3D uses it for
 * RunPushBuffer: a recorded command stream replayed by reference. This walk
 * used to step over the call word, so everything a title drew through a
 * recorded pushbuffer was never drawn. RECOMP_PB_NOCALL=1: the old behaviour.
 */
static uint32_t s_tot_calls, s_tot_returns;

/*
 * Walking a copy.
 *
 * GET is published before the walk reads the segment, and D3D reuses the
 * ring behind GET. While the title was held at KickOff until the ack
 * thread came round again, it could not get far enough to matter; with
 * the flush-ack thread letting it run on, it wrapped the ring and rewrote
 * a segment under the walk (on the Switch: a stray CALL into unmapped
 * memory). So, with that thread on, the ack thread copies what it is
 * about to acknowledge first (nv2a_pb_snapshot_take) and the walk reads
 * those words from the copy. A range the walk reaches that the copy
 * does not cover -- a call's target, a jump's landing past the head
 * window -- is copied on entry to the walk instead (snap_ensure
 * below): a subroutine can take milliseconds of GL work to walk, and
 * reading it live tears it across whatever the title writes meanwhile.
 * The escrow is one consistent moment per range, under the same fence
 * discipline the walk's data reads already rely on.
 */
#define SNAP_MAX_WIN 16
static uint8_t *s_snap;
static size_t   s_snap_cap;
static struct { uint32_t lo, hi; size_t off; } s_win[SNAP_MAX_WIN];
static int      s_nwin;
/* Set by nv2a_pb_snapshot_take: the ack loop is taking the pre-GET copy
 * this pass, so on-demand windows belong to the same moment and the
 * next pass's take resets them. Never set where nothing takes the copy
 * (the flush-ack thread off), and then the walk reads live throughout,
 * exactly as it did before the copy existed. */
static int      s_snap_live;

static void snap_add(uint32_t lo, uint32_t hi)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    size_t off = s_nwin ? s_win[s_nwin - 1].off + (s_win[s_nwin - 1].hi - s_win[s_nwin - 1].lo) : 0;
    size_t n;
    if (hi > (0x80000000u | (64u << 20))) hi = 0x80000000u | (64u << 20);
    if (s_nwin >= SNAP_MAX_WIN || hi <= lo) return;
    n = hi - lo;
    if (off + n > s_snap_cap) {
        size_t cap = (off + n + 0xFFFFu) & ~(size_t)0xFFFFu;
        uint8_t *b = (uint8_t *)realloc(s_snap, cap);
        if (!b) return;
        s_snap = b;
        s_snap_cap = cap;
    }
    memcpy(s_snap + off, mem + lo, n);
    s_win[s_nwin].lo = lo;
    s_win[s_nwin].hi = hi;
    s_win[s_nwin].off = off;
    s_nwin++;
}

/* Copy whatever of [lo, hi) the current windows do not already cover,
 * so a walk of that range reads the copy throughout. Bounded per call;
 * a range past the bound, or one that arrives with the window table
 * full, keeps the live reads it had. RECOMP_PB_CALLSNAP=0 turns the
 * on-demand copy off. */
static void snap_ensure(uint32_t lo, uint32_t hi)
{
    static int on = -1;
    uint32_t cur = lo;
    size_t budget = 0x400000u;
    if (on < 0) { const char *e = getenv("RECOMP_PB_CALLSNAP");
                  on = e ? atoi(e) != 0 : 1; }
    if (!on || !s_snap_live) return;
    if (hi > (0x80000000u | (64u << 20))) hi = 0x80000000u | (64u << 20);
    while (cur < hi) {
        uint32_t covered_to = 0, next_lo = hi;
        int i, before;
        for (i = 0; i < s_nwin; i++) {
            if (cur >= s_win[i].lo && cur < s_win[i].hi) { covered_to = s_win[i].hi; break; }
            if (s_win[i].lo > cur && s_win[i].lo < next_lo) next_lo = s_win[i].lo;
        }
        if (covered_to) { cur = covered_to; continue; }
        if (next_lo - cur > budget) next_lo = cur + (uint32_t)budget;
        if (next_lo <= cur) return;
        before = s_nwin;
        snap_add(cur, next_lo);
        if (s_nwin == before) return;   /* table full or copy refused */
        budget -= next_lo - cur;
        cur = next_lo;
    }
}

/* last_put and put as the ack loop reads them (physical). Mirrors the
 * ranges its walk will cover. */
void nv2a_pb_snapshot_take(uint32_t last_put, uint32_t put);
void nv2a_pb_snapshot_take(uint32_t last_put, uint32_t put)
{
    uint32_t from = 0x80000000u | (last_put & 0x0FFFFFFFu);
    uint32_t to   = 0x80000000u | (put & 0x0FFFFFFFu);
    s_snap_live = 1;
    s_nwin = 0;
    if (!last_put || put == last_put) return;
    if (to > from) {
        snap_add(from, to - from > 0x400000u ? from + 0x400000u : to);
    } else {
        /* The same reach as the ack loop's walk of a wrap: up to 1 MB of
         * tail to find the jump, and the head after it. They were 128 KB
         * and 256 KB, so a longer tail was read live -- after GET had
         * told the title it could reuse it. With fast_gpu that was JSRF's
         * character select drawing from rewritten words (an attribute
         * stride of 4521728, a walk off the end of RAM). */
        uint32_t back = (put & 0x0FFFFFFFu) < 0x100000u ? (put & 0x0FFFFFFFu) : 0x100000u;
        snap_add(from, from + 0x100000u);
        snap_add(to - back, to);
    }
}

/* The window holding all of [va, va + bytes), or -1. The windows never
 * overlap (snap_ensure copies only what none covers), so any one that
 * holds the range is the one. The window the last lookup found is tried
 * first: a walk reads its words in order, and almost every word comes
 * from the window the one before it did, where a scan from the first
 * window cost a compare per window on every word of the frame. */
static int s_win_last;
static inline int pb_win(uint32_t va, uint32_t bytes)
{
    int i = s_win_last;
    if (i < s_nwin && va >= s_win[i].lo && va - s_win[i].lo + bytes <= s_win[i].hi - s_win[i].lo)
        return i;
    for (i = 0; i < s_nwin; i++)
        if (va >= s_win[i].lo && va - s_win[i].lo + bytes <= s_win[i].hi - s_win[i].lo)
            return s_win_last = i;
    return -1;
}

static inline uint32_t pb_word(const uint8_t *mem, uint32_t va)
{
    int i;
    if (!s_nwin) return *(const uint32_t *)(mem + va);
    i = pb_win(va, 4);
    if (i >= 0)
        return *(const uint32_t *)(s_snap + s_win[i].off + (va - s_win[i].lo));
    return *(const uint32_t *)(mem + va);
}
static int s_in_sub;
static uint32_t pb_walk_sub(uint32_t target_va);

uint32_t nv2a_pb_scan(uint32_t start_va, uint32_t end_va)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t va = start_va;
    uint32_t words = 0, jumps = 0, unknown = 0, jump_to = 0;

    /* Both read once. This runs on every pushbuffer segment the title submits
     * -- thousands a second -- and getenv walks the environment doing a string
     * compare per entry every time it is called. */
    if (s_exec_enabled < 0)
        s_exec_enabled = getenv("RECOMP_PB_EXEC") != NULL;
    if (s_scan_enabled < 0)
        s_scan_enabled = getenv("RECOMP_PB_SCAN") != NULL;
    if (!(s_scan_enabled || s_exec_enabled) || end_va <= start_va)
        return 0;

    /*
     * How much of the frame this walk costs.
     *
     * The GL profile accounts for the GL calls and the vertex decode and finds
     * 94% of the frame somewhere else. The two remaining candidates are this
     * -- walking the pushbuffer and running every method through the backend's
     * state machine -- and the recompiled guest code itself, and they are
     * indistinguishable from the frame rate. One clock pair per segment
     * measures it without the measurement becoming the cost: a segment is
     * hundreds of methods.
     */
    { extern void nv2a_pb_time_begin(void); nv2a_pb_time_begin(); }
    if (end_va - start_va > 0x400000u)        /* a sane single-frame bound */
        end_va = start_va + 0x400000u;
    /* Never past the end of RAM's contiguous view: a walk that has lost its
     * place reads its way off the end (Switch, fast_gpu: a fault reading
     * guest 0x84000000 after JSRF's character select). */
    if (start_va >= 0x80000000u && start_va < 0x84000000u && end_va > 0x84000000u)
        end_va = 0x84000000u;
    if (start_va >= 0x84000000u && start_va < 0xF0000000u)
        end_va = start_va;                /* nothing there to walk */

    /* The submitted span is already in the copy (nv2a_pb_snapshot_take);
     * a call target or a jump's landing past the head window may not be.
     * Cover the range this walk is about to read before it reads it. */
    if (start_va >= 0x80000000u && end_va > start_va)
        snap_ensure(start_va, end_va);

    while (va < end_va && words < 0x100000u) {
        uint32_t w = pb_word(mem, va);
        va += 4;
        words++;

        if ((w & 3u) == 1u || (w & 0xE0000003u) == 0x20000000u) {
            /* A jump ends this segment. Its target is handed back so the
             * caller can carry on from there: the ring wraps with a jump
             * to its start, and the commands the title wrote after it --
             * including the fence the title then waits for -- are at the
             * target, not here. */
            jumps++;
            jump_to = ((w & 3u) == 1u) ? (w & ~3u) : (w & 0x1FFFFFFCu);
            break;
        }
        if ((w & 3u) == 2u) {
            static int nocall = -1;
            uint32_t tgt = 0x80000000u | ((w & ~3u) & 0x0FFFFFFFu);
            if (nocall < 0) nocall = getenv("RECOMP_PB_NOCALL") != NULL;
            s_tot_calls++;
            if (s_tot_calls <= 12 || (s_tot_calls & (s_tot_calls - 1)) == 0)
                fprintf(stderr, "[PB] call #%u at %08X -> %08X%s\n", s_tot_calls,
                        va - 4, tgt, s_in_sub ? " (inside a call: ignored)"
                                               : nocall ? " (RECOMP_PB_NOCALL: skipped)" : "");
            if (!s_in_sub && !nocall && s_exec_enabled) {
                uint32_t n;
                s_in_sub = 1;
                n = pb_walk_sub(tgt);
                s_in_sub = 0;
                words += n;
            }
            continue;
        }
        if ((w & 0xFFFF0003u) == 0x00020000u) {
            s_tot_returns++;
            if (s_in_sub) break;          /* back to the caller */
            continue;
        }
        if ((w & 0x00030003u) == 0u) {
            uint32_t count  = (w >> 18) & 0x7FFu;
            uint32_t subch  = (w >> 13) & 7u;
            uint32_t method =  w & 0x1FFCu;
            int noninc = (w & 0xE0000000u) == 0x40000000u;

            /* Index runs whole (nv2a_pb_exec_indices): they are most of
             * the words in a frame. RECOMP_PB_BULK=0 goes word by word. */
            if (noninc && s_exec_enabled && !s_scan_enabled && subch == 0
                && (method == 0x1800 || method == 0x1808) && count) {
                static int bulk = -1;
                static uint32_t buf[0x800];
                uint32_t n = count;
                if (bulk < 0) { const char *e = getenv("RECOMP_PB_BULK");
                                bulk = e ? atoi(e) != 0 : 1; }
                if (n > (end_va - va) / 4) n = (end_va - va) / 4;
                if (bulk && n) {
                    /* A run inside one window is handed over where it lies
                     * in the copy, which nothing changes until the next
                     * pass; with no copy at all it is read live in one go,
                     * as pb_word would; a run that straddles windows (or
                     * leaves them) is gathered word by word. */
                    const uint32_t *run = buf;
                    int wi = s_nwin ? pb_win(va, n * 4) : -1;
                    if (wi >= 0)
                        run = (const uint32_t *)(s_snap + s_win[wi].off + (va - s_win[wi].lo));
                    else if (!s_nwin)
                        memcpy(buf, mem + va, (size_t)n * 4);
                    else
                        for (uint32_t i = 0; i < n; i++) buf[i] = pb_word(mem, va + i * 4);
                    if (nv2a_pb_exec_indices(subch, method, run, n)) {
                        va += n * 4;
                        words += n;
                        continue;
                    }
                }
            }
            for (uint32_t i = 0; i < count && va < end_va; i++) {
                uint32_t m = noninc ? method : method + i * 4;
                /* The inventory is only ever printed under RECOMP_PB_SCAN, so
                 * when nobody is going to read it, do not build it. */
                if (s_scan_enabled) note(subch, m);
                /* Same walk, two consumers: the survey counts, the executor
                 * acts. Keeping them on one decode means they can never
                 * disagree about what the stream said. */
                if (s_exec_enabled)
                    nv2a_pb_exec_method(subch, m, pb_word(mem, va));
                va += 4;
                words++;
            }
            continue;
        }
        unknown++;
    }

    s_tot_words += words;
    s_tot_unknown += unknown;
    s_tot_jumps += jumps;
    s_tot_segments++;
    { extern void nv2a_pb_time_end(unsigned methods); nv2a_pb_time_end(words); }
    return jump_to;
}

/* The body of a call: from its target up to the RETURN, through the same
 * decoder (nv2a_pb_scan with s_in_sub set stops at the RETURN). A jump inside
 * a subroutine is followed once, which is how a recorded buffer longer than
 * one segment continues. Returns the words walked. */
static uint32_t pb_walk_sub(uint32_t target_va)
{
    uint32_t before = s_tot_words, j, hops = 0;
    /* A target past the end of RAM is not a call: the walk has lost its
     * place and is reading data as commands. Following it faulted the
     * Switch build (a CALL to 0x84AF04AC, 75 MB in). */
    if ((target_va & 0x0FFFFFFFu) >= 64u * 1024u * 1024u) return 0;
    j = nv2a_pb_scan(target_va, target_va + 0x400000u);
    while (j && hops++ < 64) {
        uint32_t t = 0x80000000u | (j & 0x0FFFFFFFu);
        /* The entry guard's test, one hop later: a jump target past the
         * end of RAM means the walk lost its place in the subroutine and
         * is reading data as commands. Following one faulted the Switch
         * build during a stage load (a hop to 0x8F800000), the same
         * failure the guard above was written for, one level down. */
        if ((t & 0x0FFFFFFFu) >= 64u * 1024u * 1024u) break;
        j = nv2a_pb_scan(t, t + 0x400000u);
    }
    return s_tot_words - before;
}
