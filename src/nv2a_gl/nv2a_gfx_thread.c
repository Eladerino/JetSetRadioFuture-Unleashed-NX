/*
 * The device on a thread of its own (RECOMP_GL_THREAD=1; on by default on
 * the Switch, where RECOMP_GL_THREAD=0 turns it off).
 *
 * In a heavy scene the GPU thread -- the one that walks the pushbuffer and
 * keeps the front half's state -- is the frame's limit, and about forty per
 * cent of its time is spent inside the driver: draw calls, vertex and index
 * uploads, textures. This device takes every call the front half makes
 * (nv2a_backend.h), writes it with everything it points at into a ring, and
 * a worker thread that owns the OpenGL context replays it on the real
 * device (nv2a_gfx_gl.c). The walk goes on decoding the next draw while the
 * driver works on the last one.
 *
 * Handles. Targets, textures, programs and vertex buffers are named by this
 * device's own numbers, handed out at once; the worker keeps which real
 * handle each one stands for. A number is given out again only after its
 * free has been written into the ring, so the worker always sees the free
 * before the next use.
 *
 * Calls that answer. Reading pixels back and the device's capabilities wait
 * for the worker to get there (sync). Making a program waits too: the front
 * half needs to know whether it failed, and building one is slow either
 * way; the answer comes back with what the program reads, which prog_uses
 * and uniform_f_part answer from afterwards without asking again. The
 * statistics (pipeline_ms, gpu_ms, cache_idle's count) are the worker's
 * last word.
 *
 * Frames. The window's present (nv2a_gl.c, sdl_present: the blit, the menu,
 * the swap) is a ring entry like any other, run by the worker; the flip
 * waits while the worker is more than one frame behind, which bounds how far
 * the picture lags and how much the ring holds.
 *
 * One producer (the GPU thread) and one consumer (the worker). Each side
 * spins briefly and then sleeps on a condition when it has to wait; the
 * other wakes it only when it said it was asleep.
 */

#include "nv2a_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(NV2A_GL_USE_SDL)
#include <SDL2/SDL.h>

int nv2a_gfx_gl_prog_info(uint32_t h, uint64_t *uses, int *csize);   /* nv2a_gfx_gl.c */
int nv2a_gfx_gl_dxt_ok(int dxt, const void *blocks, const Nv2aSampler *s);
int nv2a_gfx_gl_vbufs_max(void);

/* On the Switch, which core the worker runs on (port/switch). */
extern void sw_thread_pin(const char *what, unsigned id, int core) __attribute__((weak));

/* ---- the ring -------------------------------------------------------------- */

enum {
    OP_WRAP = 1, OP_TARGET_NEW, OP_TARGET_FREE, OP_TARGET_BIND, OP_TARGET_TEXTURE,
    OP_TARGET_SNAPSHOT, OP_TARGET_READ, OP_TARGET_READ_DEPTH, OP_TARGET_SMAA,
    OP_TEX_NEW, OP_TEX_NEW_DXT, OP_TEX_FREE, OP_TEX_READ,
    OP_PROG_NEW, OP_PROG_FREE, OP_PROG_USE,
    OP_UNIFORM_F, OP_UNIFORM_I, OP_UNIFORM_F_PART,
    OP_TEXTURE, OP_STATE, OP_STATE_INVALIDATE,
    OP_VERTICES, OP_DRAW, OP_DRAW_INDEXED, OP_CLEAR, OP_PRESENT, OP_REPORT,
    OP_VBUF_NEW, OP_VBUF_FREE, OP_VBUF_FILL, OP_VBUF_BIND,
    OP_CACHE_OPEN, OP_CACHE_IDLE, OP_SAMPLES_MAX, OP_SAMPLES, OP_ZREV,
    OP_RUN, OP_FRAME
};

typedef struct { uint32_t op, len; uint64_t seq; } Hdr;   /* len: bytes after the header */

#define RING_BYTES   (32u << 20)
#define BIG_BYTES    (1u << 20)      /* a payload this size or more goes on the heap */

static uint8_t *g_ring;
static volatile uint64_t g_w, g_r;      /* bytes written / taken, ever */
static uint64_t g_seq;                  /* entries written (producer) */
static volatile uint64_t g_done;        /* entries done (worker) */
static volatile uint64_t g_frames_in, g_frames_done;

static SDL_mutex *g_mu;
static SDL_cond  *g_cv_work, *g_cv_prod;
static volatile int g_work_sleeps, g_prod_sleeps;

static const Nv2aGfx *R;                /* the real device */
static Nv2aGfx g_dev;                   /* this one */
static int g_on;

static int (*g_ctx_take)(void);         /* make the context current here */
static volatile int g_took;             /* the worker: 1 it has the context, -1 not */

/* What sync calls hand back. */
static volatile intptr_t g_reply;
static volatile uint64_t g_reply_uses;
static volatile int g_reply_csize;

/* The worker's last word on the statistics. */
static volatile double g_pipeline_ms, g_gpu_busy, g_gpu_waited;
static volatile int g_cache_left = 1;

static inline uint64_t ld(const volatile uint64_t *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static inline void st(volatile uint64_t *p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

static void wake_worker(void)
{
    if (__atomic_load_n(&g_work_sleeps, __ATOMIC_SEQ_CST)) {
        SDL_LockMutex(g_mu); SDL_CondSignal(g_cv_work); SDL_UnlockMutex(g_mu);
    }
}
static void wake_producer(void)
{
    if (__atomic_load_n(&g_prod_sleeps, __ATOMIC_SEQ_CST)) {
        SDL_LockMutex(g_mu); SDL_CondSignal(g_cv_prod); SDL_UnlockMutex(g_mu);
    }
}

/* ---- RECOMP_GL_SPLIT: the worker's account of itself -----------------------
 *
 * The GLPROF categories in nv2a_gl.c time the device calls on the walk
 * thread, where a call is a ring write; the driver time is paid here, on
 * the worker, and the worker used to report none of it. With
 * RECOMP_GL_SPLIT=1 the worker times its own replay, grouped the way the
 * driver question is asked (draws, binds, uploads, program creation,
 * present, the rest), and the producer adds up what it spends waiting
 * for the worker in PROD_WAIT below. nv2a_gl.c reads both back once a
 * second through nv2a_gfx_thread_split and prints them beside the rest
 * of GLPROF. Off, which is the default, nothing here runs: the worker
 * takes no clock reads and the producer waits exactly as before. */
enum { SPLIT_DRAWS, SPLIT_BINDS, SPLIT_UPLOADS, SPLIT_PROGNEW,
       SPLIT_PRESENT, SPLIT_OTHER, SPLIT_GROUPS };

static int g_split_on = -1;
static double g_split_ms[SPLIT_GROUPS];        /* the worker's */
static unsigned long g_split_n[SPLIT_GROUPS];  /* the worker's */
static double g_split_stall;                   /* the producer's */

static int split_on(void)
{
    if (g_split_on < 0) g_split_on = getenv("RECOMP_GL_SPLIT") ? 1 : 0;
    return g_split_on;
}

static double split_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static int split_group(uint32_t op)
{
    switch (op) {
    case OP_DRAW: case OP_DRAW_INDEXED: return SPLIT_DRAWS;
    case OP_PROG_USE: case OP_TEXTURE: case OP_STATE: return SPLIT_BINDS;
    case OP_VERTICES: case OP_VBUF_FILL:
    case OP_TEX_NEW: case OP_TEX_NEW_DXT:
    case OP_UNIFORM_F: case OP_UNIFORM_F_PART: case OP_UNIFORM_I:
        return SPLIT_UPLOADS;
    case OP_PROG_NEW: return SPLIT_PROGNEW;
    case OP_PRESENT: return SPLIT_PRESENT;
    default: return SPLIT_OTHER;
    }
}

/* The producer waits for `ok` to hold: a spin, then sleep. With the split
 * on, a wait that actually waits is timed: the stall is the part of the
 * worker's pace the frame pays for directly, where replay the walk never
 * waits on is hidden behind the next segment's decode. */
#define PROD_WAIT(ok) do {                                                    \
    if (!(ok)) {                                                              \
        int pw_on_ = split_on();                                              \
        double pw_t0_ = pw_on_ ? split_now() : 0.0;                           \
        int spin_ = 0;                                                        \
        while (!(ok)) {                                                       \
            if (++spin_ < 4000) continue;                                     \
            SDL_LockMutex(g_mu);                                              \
            __atomic_store_n(&g_prod_sleeps, 1, __ATOMIC_SEQ_CST);            \
            if (!(ok)) SDL_CondWaitTimeout(g_cv_prod, g_mu, 2);               \
            __atomic_store_n(&g_prod_sleeps, 0, __ATOMIC_SEQ_CST);            \
            SDL_UnlockMutex(g_mu);                                            \
        }                                                                     \
        if (pw_on_) g_split_stall += split_now() - pw_t0_;                    \
    }                                                                         \
} while (0)

static uint64_t g_cur_at;          /* where the entry being written starts */
static uint32_t g_cur_len;

/* Room for an entry with `len` bytes of payload; returns where to write it. */
static uint8_t *put_begin(uint32_t op, uint32_t len)
{
    uint32_t need = (uint32_t)sizeof(Hdr) + ((len + 15u) & ~15u);   /* every entry 16-aligned */
    uint64_t at = g_w;
    uint32_t off = (uint32_t)(at % RING_BYTES);
    Hdr *h;
    if (off + need > RING_BYTES) {               /* to the start: a wrap marker */
        uint32_t skip = RING_BYTES - off;
        PROD_WAIT(RING_BYTES - (at - ld(&g_r)) >= (uint64_t)skip + need);
        h = (Hdr *)(g_ring + off);
        h->op = OP_WRAP; h->len = skip - (uint32_t)sizeof(Hdr); h->seq = 0;
        at += skip;
        st(&g_w, at);
        off = 0;
    }
    PROD_WAIT(RING_BYTES - (at - ld(&g_r)) >= need);
    h = (Hdr *)(g_ring + off);
    h->op = op; h->len = need - (uint32_t)sizeof(Hdr); h->seq = ++g_seq;
    g_cur_at = at; g_cur_len = need;
    return (uint8_t *)(h + 1);
}
static uint64_t put_end(void)
{
    st(&g_w, g_cur_at + g_cur_len);
    wake_worker();
    return g_seq;
}
static void wait_done(uint64_t seq)
{
    PROD_WAIT(ld(&g_done) >= seq);
}

/* A payload builder: fixed part first, then data blobs, each 8-aligned. */
typedef struct { uint8_t *p; } Out;
static void *out_take(Out *o, size_t n) { void *q = o->p; o->p += (n + 7u) & ~(size_t)7u; return q; }
static size_t al(size_t n) { return (n + 7u) & ~(size_t)7u; }

/* What a call points at: NULL, its bytes in the ring after the payload's
 * fixed part, or -- too big for that -- a copy on the heap that the worker
 * frees. */
enum { B_NULL, B_INLINE, B_HEAP, B_EMPTY };
typedef struct { const void *heap; uint32_t bytes, how; } Blob;
static const uint64_t g_empty[2];
static size_t blob_room(size_t bytes) { return bytes >= BIG_BYTES ? 0 : al(bytes); }
static void blob_put(Out *o, Blob *b, const void *src, size_t bytes)
{
    b->bytes = (uint32_t)bytes;
    b->heap = NULL;
    if (!src) { b->how = B_NULL; return; }
    if (!bytes) { b->how = B_EMPTY; return; }
    if (bytes >= BIG_BYTES) {
        void *m = malloc(bytes);
        if (m) memcpy(m, src, bytes);
        b->heap = m; b->how = m ? B_HEAP : B_NULL;
    } else {
        memcpy(out_take(o, bytes), src, bytes);
        b->how = B_INLINE;
    }
}
/* The worker's side: where the bytes are. Blobs are taken in the order
 * they were put. */
static const void *blob_get(const Blob *b, const uint8_t **cur)
{
    const void *p;
    switch (b->how) {
    case B_INLINE: p = *cur; *cur += al(b->bytes); return p;
    case B_HEAP:   return b->heap;
    case B_EMPTY:  return g_empty;
    default:       return NULL;
    }
}
static void blob_done(const Blob *b) { if (b->how == B_HEAP) free((void *)b->heap); }

/* ---- handles ---------------------------------------------------------------
 * Our numbers, by kind. The producer hands them out and takes them back; the
 * worker's map says what each stands for. */
enum { K_RT, K_TEX, K_PROG, K_VB, K_KINDS };
typedef struct { uint32_t *free_ids, nfree, cap_free, top; } Ids;
static Ids g_ids[K_KINDS];
static uint32_t *g_map[K_KINDS], g_map_cap[K_KINDS];     /* the worker's */

static uint32_t id_new(int k)
{
    Ids *d = &g_ids[k];
    if (d->nfree) return d->free_ids[--d->nfree];
    return ++d->top;
}
static void id_free(int k, uint32_t id)
{
    Ids *d = &g_ids[k];
    if (!id) return;
    if (d->nfree == d->cap_free) {
        uint32_t c = d->cap_free ? d->cap_free * 2 : 256;
        uint32_t *n = (uint32_t *)realloc(d->free_ids, c * sizeof *n);
        if (!n) return;                            /* the number is simply not reused */
        d->free_ids = n; d->cap_free = c;
    }
    d->free_ids[d->nfree++] = id;
}
static uint32_t real_of(int k, uint32_t id) { return id && id < g_map_cap[k] ? g_map[k][id] : 0; }
static void map_set(int k, uint32_t id, uint32_t real)
{
    if (!id) return;
    if (id >= g_map_cap[k]) {
        uint32_t c = g_map_cap[k] ? g_map_cap[k] : 1024;
        uint32_t *n;
        while (c <= id) c *= 2;
        n = (uint32_t *)realloc(g_map[k], c * sizeof *n);
        if (!n) return;
        memset(n + g_map_cap[k], 0, (c - g_map_cap[k]) * sizeof *n);
        g_map[k] = n; g_map_cap[k] = c;
    }
    g_map[k][id] = real;
}

/* The producer's notes on its handles: a target's texture and its SMAA
 * copy are each one number for good; what each program reads. */
typedef struct { uint32_t tex, smaa; } RtNote;
static RtNote *g_rt_note; static uint32_t g_rt_note_cap;
static RtNote *rt_note(uint32_t rt)
{
    if (rt >= g_rt_note_cap) {
        uint32_t c = g_rt_note_cap ? g_rt_note_cap : 256;
        RtNote *n;
        while (c <= rt) c *= 2;
        n = (RtNote *)realloc(g_rt_note, c * sizeof *n);
        if (!n) return NULL;
        memset(n + g_rt_note_cap, 0, (c - g_rt_note_cap) * sizeof *n);
        g_rt_note = n; g_rt_note_cap = c;
    }
    return &g_rt_note[rt];
}
typedef struct { uint64_t uses; int csize, live; } ProgNote;
static ProgNote *g_prog_note; static uint32_t g_prog_note_cap;
static ProgNote *prog_note(uint32_t p)
{
    if (p >= g_prog_note_cap) {
        uint32_t c = g_prog_note_cap ? g_prog_note_cap : 256;
        ProgNote *n;
        while (c <= p) c *= 2;
        n = (ProgNote *)realloc(g_prog_note, c * sizeof *n);
        if (!n) return NULL;
        memset(n + g_prog_note_cap, 0, (c - g_prog_note_cap) * sizeof *n);
        g_prog_note = n; g_prog_note_cap = c;
    }
    return &g_prog_note[p];
}
static uint32_t g_cur_prog;          /* the producer's view of the program in use */
static uint32_t g_snap_tex;          /* target_snapshot's one texture */

/* ---- payloads ------------------------------------------------------------- */

typedef struct { uint32_t a, b, c, d; } P4;
typedef struct { uint32_t rt, x, y, w, h; int bgra; void *out; } PRead;
typedef struct { uint32_t rt, pw, ph; float *out; } PReadDepth;
typedef struct { uint32_t id, w, h; int dxt; Nv2aSampler s; Blob data; } PTex;
typedef struct { uint32_t tex, w, h; void *out; } PTexRead;
typedef struct { Blob v, f, what; } PProg;
typedef struct { int id, n, first; Blob v; } PUni;
typedef struct { uint32_t nverts, stride; int attr_at[16]; uint32_t const_mask;
                 float const_attr[16][4]; Blob v; } PVerts;
typedef struct { unsigned mask; float rgba[4]; float depth; uint32_t stencil, rect[4];
                 int has_rgba, has_rect; } PClear;
typedef struct { uint32_t vb; Blob v, idx; uint32_t vbytes, ibytes; } PFill;
typedef struct { uint32_t vb, stride; int attr_at[16]; uint32_t const_mask;
                 float const_attr[16][4]; } PBind;
typedef struct { Blob dir, tag; } PCache;
typedef struct { void (*fn)(const void *arg); uint32_t bytes; } PRun;

static uint32_t uniform_floats(int id, int n)
{
    static const uint8_t per[] = { 1, 2, 3, 4, 1, 4, 1 };   /* NV2A_UT_* */
    if (id < 0 || id >= NV2A_U_COUNT || n <= 0) return 0;
    return (uint32_t)n * per[nv2a_uniform_desc[id].type < sizeof per ? nv2a_uniform_desc[id].type : 3];
}

/* tex_new's and tex_new_dxt's bytes: level 0, and the rest of the chain
 * where the device reads it from the same pointer (nv2a_gfx_gl.c's
 * glx_tex_make). */
static size_t tex_bytes(uint32_t w, uint32_t h, int dxt, const Nv2aSampler *s)
{
    size_t n = 0;
    uint32_t l, levels = (s && s->mip && s->levels > 1) ? s->levels : 1;
    for (l = 0; l < levels; l++) {
        n += dxt ? (size_t)((w + 3) / 4) * ((h + 3) / 4) * (dxt == 1 ? 8 : 16)
                 : (size_t)w * h * 4;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return n;
}

/* ---- the producer's device -------------------------------------------------- */

static uint32_t t_target_new(uint32_t pw, uint32_t ph)
{
    uint32_t id = id_new(K_RT);
    P4 *p = (P4 *)put_begin(OP_TARGET_NEW, sizeof *p);
    p->a = id; p->b = pw; p->c = ph;
    wait_done(put_end());
    if (!g_reply) { id_free(K_RT, id); return 0; }
    { RtNote *n = rt_note(id); if (n) n->tex = 0; }   /* its SMAA number, if any, stays */
    return id;
}
static void t_target_free(uint32_t rt)
{
    P4 *p = (P4 *)put_begin(OP_TARGET_FREE, sizeof *p);
    RtNote *n = rt_note(rt);
    p->a = rt;
    put_end();
    if (n) {
        if (n->tex) id_free(K_TEX, n->tex);
        n->tex = 0;      /* the SMAA copy belongs to the device: kept, number and all */
    }
    id_free(K_RT, rt);
}
static void t_target_bind(uint32_t rt)
{
    P4 *p = (P4 *)put_begin(OP_TARGET_BIND, sizeof *p);
    p->a = rt;
    put_end();
}
static uint32_t t_target_texture(uint32_t rt)
{
    RtNote *n = rt_note(rt);
    P4 *p;
    if (!n) return 0;
    if (!n->tex) n->tex = id_new(K_TEX);
    p = (P4 *)put_begin(OP_TARGET_TEXTURE, sizeof *p);
    p->a = rt; p->b = n->tex;
    put_end();
    return n->tex;
}
static uint32_t t_target_snapshot(uint32_t rt)
{
    P4 *p;
    if (!g_snap_tex) g_snap_tex = id_new(K_TEX);
    p = (P4 *)put_begin(OP_TARGET_SNAPSHOT, sizeof *p);
    p->a = rt; p->b = g_snap_tex;
    put_end();
    return g_snap_tex;
}
static int t_target_read(uint32_t rt, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         int bgra, void *out)
{
    PRead *p = (PRead *)put_begin(OP_TARGET_READ, sizeof *p);
    p->rt = rt; p->x = x; p->y = y; p->w = w; p->h = h; p->bgra = bgra; p->out = out;
    wait_done(put_end());
    return (int)g_reply;
}
static int t_target_read_depth(uint32_t rt, uint32_t pw, uint32_t ph, float *out)
{
    PReadDepth *p = (PReadDepth *)put_begin(OP_TARGET_READ_DEPTH, sizeof *p);
    p->rt = rt; p->pw = pw; p->ph = ph; p->out = out;
    wait_done(put_end());
    return (int)g_reply;
}
static uint32_t t_target_smaa(uint32_t rt)
{
    RtNote *n = rt_note(rt);
    P4 *p;
    if (!n) return rt;
    if (!n->smaa) n->smaa = id_new(K_RT);
    p = (P4 *)put_begin(OP_TARGET_SMAA, sizeof *p);
    p->a = rt; p->b = n->smaa;
    put_end();
    return n->smaa;
}

static uint32_t tex_put(int op, uint32_t w, uint32_t h, int dxt, const void *data,
                        const Nv2aSampler *s)
{
    size_t bytes = data ? tex_bytes(w, h, dxt, s) : 0;
    uint32_t id = id_new(K_TEX);
    Out o;
    PTex *p = (PTex *)put_begin((uint32_t)op, (uint32_t)(al(sizeof *p) + blob_room(bytes)));
    o.p = (uint8_t *)p + al(sizeof *p);
    p->id = id; p->w = w; p->h = h; p->dxt = dxt; p->s = *s;
    blob_put(&o, &p->data, data, bytes);
    put_end();
    return id;
}
static uint32_t t_tex_new(uint32_t w, uint32_t h, const void *rgba, const Nv2aSampler *s)
{
    return tex_put(OP_TEX_NEW, w, h, 0, rgba, s);
}
static uint32_t t_tex_new_dxt(uint32_t w, uint32_t h, int dxt, const void *blocks,
                              const Nv2aSampler *s)
{
    if (!nv2a_gfx_gl_dxt_ok(dxt, blocks, s)) return 0;      /* the front half decodes it */
    return tex_put(OP_TEX_NEW_DXT, w, h, dxt, blocks, s);
}
static void t_tex_free(uint32_t tex)
{
    P4 *p = (P4 *)put_begin(OP_TEX_FREE, sizeof *p);
    p->a = tex;
    put_end();
    id_free(K_TEX, tex);
}
static int t_tex_read(uint32_t tex, uint32_t w, uint32_t h, void *bgra)
{
    PTexRead *p = (PTexRead *)put_begin(OP_TEX_READ, sizeof *p);
    p->tex = tex; p->w = w; p->h = h; p->out = bgra;
    wait_done(put_end());
    return (int)g_reply;
}

static uint32_t t_prog_new(const char *vsrc, const char *fsrc, const char *what)
{
    size_t lv = vsrc ? strlen(vsrc) + 1 : 0, lf = fsrc ? strlen(fsrc) + 1 : 0,
           lw = what ? strlen(what) + 1 : 0;
    uint32_t id = id_new(K_PROG);
    Out o;
    PProg *p = (PProg *)put_begin(OP_PROG_NEW,
        (uint32_t)(al(sizeof *p) + al(sizeof id) + blob_room(lv) + blob_room(lf) + blob_room(lw)));
    uint32_t *idp;
    o.p = (uint8_t *)p + al(sizeof *p);
    idp = (uint32_t *)out_take(&o, sizeof id);
    *idp = id;
    blob_put(&o, &p->v, vsrc, lv);
    blob_put(&o, &p->f, fsrc, lf);
    blob_put(&o, &p->what, what, lw);
    wait_done(put_end());
    if (!g_reply) { id_free(K_PROG, id); return 0; }
    { ProgNote *n = prog_note(id); if (n) { n->uses = g_reply_uses; n->csize = g_reply_csize; n->live = 1; } }
    return id;
}
static void t_prog_free(uint32_t prog)
{
    P4 *p = (P4 *)put_begin(OP_PROG_FREE, sizeof *p);
    p->a = prog;
    put_end();
    if (g_cur_prog == prog) g_cur_prog = 0;
    { ProgNote *n = prog < g_prog_note_cap ? &g_prog_note[prog] : NULL;
      if (!n || !n->live) return;            /* not one of ours: nothing to give back */
      n->uses = 0; n->csize = 0; n->live = 0; }
    id_free(K_PROG, prog);
}
static int t_prog_uses(uint32_t prog, int id)
{
    ProgNote *n = prog < g_prog_note_cap ? &g_prog_note[prog] : NULL;
    return n && id >= 0 && id < NV2A_U_COUNT && ((n->uses >> id) & 1u);
}
static void t_prog_use(uint32_t prog)
{
    P4 *p = (P4 *)put_begin(OP_PROG_USE, sizeof *p);
    p->a = prog;
    put_end();
    /* As the GL device does: a program that is not one keeps the last. */
    if (prog < g_prog_note_cap && g_prog_note[prog].live) g_cur_prog = prog;
}

static void uni_put(int op, int id, int first, const float *v, int n, uint32_t floats)
{
    Out o;
    PUni *p = (PUni *)put_begin((uint32_t)op, (uint32_t)(al(sizeof *p) + blob_room(floats * 4u)));
    o.p = (uint8_t *)p + al(sizeof *p);
    p->id = id; p->n = n; p->first = first;
    blob_put(&o, &p->v, floats ? v : NULL, floats * 4u);
    put_end();
}
static void t_uniform_f(int id, const float *v, int n)
{
    uni_put(OP_UNIFORM_F, id, 0, v, n, v ? uniform_floats(id, n) : 0);
}
static void t_uniform_i(int id, int v)
{
    P4 *p = (P4 *)put_begin(OP_UNIFORM_I, sizeof *p);
    p->a = (uint32_t)id; p->b = (uint32_t)v;
    put_end();
}
/* The GL device's answer (glx_uniform_f_part), worked out here from what the
 * program in use reads; the write itself goes to the worker. */
static int t_uniform_f_part(int id, int first, const float *v, int n)
{
    static int off = -1;
    ProgNote *pn = g_cur_prog < g_prog_note_cap ? &g_prog_note[g_cur_prog] : NULL;
    if (off < 0) { const char *e = getenv("RECOMP_GL_CONST_PART"); off = (e && e[0] == '0') ? 1 : 0; }
    if (off) return 0;
    if (!g_cur_prog || !pn || id != NV2A_U_C || first < 0 || n < 0) return 0;
    if (!((pn->uses >> NV2A_U_C) & 1u)) return 1;     /* not read at all */
    if (pn->csize <= 0) return 0;
    if (first >= pn->csize) return 1;
    if (first + n > pn->csize) n = pn->csize - first;
    if (n > 0) uni_put(OP_UNIFORM_F_PART, id, first, v, n, (uint32_t)n * 4u);
    return 1;
}

static void t_texture(int unit, uint32_t tex)
{
    P4 *p = (P4 *)put_begin(OP_TEXTURE, sizeof *p);
    p->a = (uint32_t)unit; p->b = tex;
    put_end();
}
static void t_state(const Nv2aRenderState *rs)
{
    Nv2aRenderState *p = (Nv2aRenderState *)put_begin(OP_STATE, sizeof *p);
    *p = *rs;
    put_end();
}
static void t_state_invalidate(void)
{
    put_begin(OP_STATE_INVALIDATE, 0);
    put_end();
}
static void t_vertices(const float *v, uint32_t nverts, uint32_t stride_floats,
                       const int attr_at[16], uint32_t const_mask,
                       const float const_attr[16][4])
{
    size_t bytes = (size_t)nverts * stride_floats * sizeof(float);
    Out o;
    PVerts *p = (PVerts *)put_begin(OP_VERTICES, (uint32_t)(al(sizeof *p) + blob_room(bytes)));
    o.p = (uint8_t *)p + al(sizeof *p);
    p->nverts = nverts; p->stride = stride_floats;
    memcpy(p->attr_at, attr_at, sizeof p->attr_at);
    p->const_mask = const_mask;
    memcpy(p->const_attr, const_attr, sizeof p->const_attr);
    blob_put(&o, &p->v, v, bytes);
    put_end();
}
static void t_draw(int prim, uint32_t count)
{
    P4 *p = (P4 *)put_begin(OP_DRAW, sizeof *p);
    p->a = (uint32_t)prim; p->b = count;
    put_end();
}
static void t_draw_indexed(int prim, uint32_t count, int idx32)
{
    P4 *p = (P4 *)put_begin(OP_DRAW_INDEXED, sizeof *p);
    p->a = (uint32_t)prim; p->b = count; p->c = (uint32_t)idx32;
    put_end();
}
static void t_clear(unsigned mask, const float rgba[4], float depth, uint32_t stencil,
                    const uint32_t rect[4])
{
    PClear *p = (PClear *)put_begin(OP_CLEAR, sizeof *p);
    p->mask = mask; p->depth = depth; p->stencil = stencil;
    p->has_rgba = rgba != NULL; p->has_rect = rect != NULL;
    if (rgba) memcpy(p->rgba, rgba, sizeof p->rgba);
    if (rect) memcpy(p->rect, rect, sizeof p->rect);
    put_end();
}
static void t_present(uint32_t rt, uint32_t pw, uint32_t ph)
{
    P4 *p = (P4 *)put_begin(OP_PRESENT, sizeof *p);
    p->a = rt; p->b = pw; p->c = ph;
    put_end();
}
static void t_report(void)       /* at the end, often just before exit: waited for */
{
    put_begin(OP_REPORT, 0);
    wait_done(put_end());
}
static int g_vb_live;
static uint32_t t_vbuf_new(int stream)
{
    uint32_t id;
    if (g_vb_live >= nv2a_gfx_gl_vbufs_max()) return 0;      /* as the GL device would */
    g_vb_live++;
    id = id_new(K_VB);
    P4 *p = (P4 *)put_begin(OP_VBUF_NEW, sizeof *p);
    p->a = id; p->b = (uint32_t)stream;
    put_end();
    return id;
}
static void t_vbuf_free(uint32_t vb)
{
    P4 *p;
    if (!vb) return;
    p = (P4 *)put_begin(OP_VBUF_FREE, sizeof *p);
    p->a = vb;
    put_end();
    g_vb_live--;
    id_free(K_VB, vb);
}
static void t_vbuf_fill(uint32_t vb, const float *v, uint32_t vbytes, const void *idx,
                        uint32_t ibytes)
{
    Out o;
    PFill *p = (PFill *)put_begin(OP_VBUF_FILL,
        (uint32_t)(al(sizeof *p) + blob_room(vbytes) + blob_room(ibytes)));
    o.p = (uint8_t *)p + al(sizeof *p);
    p->vb = vb; p->vbytes = vbytes; p->ibytes = ibytes;
    blob_put(&o, &p->v, v, vbytes);
    blob_put(&o, &p->idx, idx, ibytes);
    put_end();
}
static void t_vbuf_bind(uint32_t vb, uint32_t stride_floats, const int attr_at[16],
                        uint32_t const_mask, const float const_attr[16][4])
{
    PBind *p = (PBind *)put_begin(OP_VBUF_BIND, sizeof *p);
    p->vb = vb; p->stride = stride_floats;
    memcpy(p->attr_at, attr_at, sizeof p->attr_at);
    p->const_mask = const_mask;
    memcpy(p->const_attr, const_attr, sizeof p->const_attr);
    put_end();
}
static double t_pipeline_ms(void) { return g_pipeline_ms; }
static void t_cache_open(const char *dir, const char *tag)
{
    size_t ld_ = dir ? strlen(dir) + 1 : 0, lt = tag ? strlen(tag) + 1 : 0;
    Out o;
    PCache *p = (PCache *)put_begin(OP_CACHE_OPEN,
        (uint32_t)(al(sizeof *p) + blob_room(ld_) + blob_room(lt)));
    o.p = (uint8_t *)p + al(sizeof *p);
    blob_put(&o, &p->dir, dir, ld_);
    blob_put(&o, &p->tag, tag, lt);
    put_end();
}
static int t_cache_idle(double budget_ms)
{
    double *p = (double *)put_begin(OP_CACHE_IDLE, sizeof *p);
    *p = budget_ms;
    put_end();
    return g_cache_left;
}
static int sync_int(uint32_t op, int arg)
{
    P4 *p = (P4 *)put_begin(op, sizeof *p);
    p->a = (uint32_t)arg;
    wait_done(put_end());
    return (int)g_reply;
}
static int t_samples_max(void) { return sync_int(OP_SAMPLES_MAX, 0); }
static int t_samples(int n) { return sync_int(OP_SAMPLES, n); }
static int t_zrev(int want) { return sync_int(OP_ZREV, want); }
static void t_gpu_ms(double *busy, double *waited) { *busy = g_gpu_busy; *waited = g_gpu_waited; }

/* ---- the worker -------------------------------------------------------------- */

static void run_one(const Hdr *h)
{
    const uint8_t *b = (const uint8_t *)(h + 1);
    const P4 *q = (const P4 *)b;
    const uint8_t *cur = b;
    switch (h->op) {
    case OP_TARGET_NEW: {
        uint32_t real = R->target_new(q->b, q->c);
        map_set(K_RT, q->a, real);
        g_reply = real != 0;
        break; }
    case OP_TARGET_FREE:
        R->target_free(real_of(K_RT, q->a));
        map_set(K_RT, q->a, 0);
        break;
    case OP_TARGET_BIND: R->target_bind(real_of(K_RT, q->a)); break;
    case OP_TARGET_TEXTURE: map_set(K_TEX, q->b, R->target_texture(real_of(K_RT, q->a))); break;
    case OP_TARGET_SNAPSHOT: map_set(K_TEX, q->b, R->target_snapshot(real_of(K_RT, q->a))); break;
    case OP_TARGET_READ: {
        const PRead *p = (const PRead *)b;
        g_reply = R->target_read(real_of(K_RT, p->rt), p->x, p->y, p->w, p->h, p->bgra, p->out);
        break; }
    case OP_TARGET_READ_DEPTH: {
        const PReadDepth *p = (const PReadDepth *)b;
        g_reply = R->target_read_depth ? R->target_read_depth(real_of(K_RT, p->rt), p->pw, p->ph, p->out) : 0;
        break; }
    case OP_TARGET_SMAA:
        map_set(K_RT, q->b, R->target_smaa(real_of(K_RT, q->a)));
        break;
    case OP_TEX_NEW: case OP_TEX_NEW_DXT: {
        const PTex *p = (const PTex *)b;
        const void *data;
        cur = b + al(sizeof *p);
        data = blob_get(&p->data, &cur);
        map_set(K_TEX, p->id, h->op == OP_TEX_NEW ? R->tex_new(p->w, p->h, data, &p->s)
                                                  : R->tex_new_dxt(p->w, p->h, p->dxt, data, &p->s));
        blob_done(&p->data);
        break; }
    case OP_TEX_FREE:
        R->tex_free(real_of(K_TEX, q->a));
        map_set(K_TEX, q->a, 0);
        break;
    case OP_TEX_READ: {
        const PTexRead *p = (const PTexRead *)b;
        g_reply = R->tex_read ? R->tex_read(real_of(K_TEX, p->tex), p->w, p->h, p->out) : 0;
        break; }
    case OP_PROG_NEW: {
        const PProg *p = (const PProg *)b;
        const char *v, *f, *w;
        uint32_t id, real;
        uint64_t uses = 0; int csize = 0;
        cur = b + al(sizeof *p);
        id = *(const uint32_t *)cur; cur += al(sizeof id);
        v = (const char *)blob_get(&p->v, &cur);
        f = (const char *)blob_get(&p->f, &cur);
        w = (const char *)blob_get(&p->what, &cur);
        real = R->prog_new(v, f, w);
        if (real) nv2a_gfx_gl_prog_info(real, &uses, &csize);
        map_set(K_PROG, id, real);
        g_reply_uses = uses; g_reply_csize = csize;
        g_reply = real != 0;
        blob_done(&p->v); blob_done(&p->f); blob_done(&p->what);
        break; }
    case OP_PROG_FREE:
        R->prog_free(real_of(K_PROG, q->a));
        map_set(K_PROG, q->a, 0);
        break;
    case OP_PROG_USE: R->prog_use(real_of(K_PROG, q->a)); break;
    case OP_UNIFORM_F: case OP_UNIFORM_F_PART: {
        const PUni *p = (const PUni *)b;
        const float *v;
        cur = b + al(sizeof *p);
        v = (const float *)blob_get(&p->v, &cur);
        if (h->op == OP_UNIFORM_F) R->uniform_f(p->id, v, p->n);
        else if (R->uniform_f_part) R->uniform_f_part(p->id, p->first, v, p->n);
        blob_done(&p->v);
        break; }
    case OP_UNIFORM_I: R->uniform_i((int)q->a, (int)q->b); break;
    case OP_TEXTURE: R->texture((int)q->a, real_of(K_TEX, q->b)); break;
    case OP_STATE: R->state((const Nv2aRenderState *)b); break;
    case OP_STATE_INVALIDATE: R->state_invalidate(); break;
    case OP_VERTICES: {
        const PVerts *p = (const PVerts *)b;
        const float *v;
        cur = b + al(sizeof *p);
        v = (const float *)blob_get(&p->v, &cur);
        R->vertices(v, p->nverts, p->stride, p->attr_at, p->const_mask, p->const_attr);
        blob_done(&p->v);
        break; }
    case OP_DRAW: R->draw((int)q->a, q->b); break;
    case OP_DRAW_INDEXED: R->draw_indexed((int)q->a, q->b, (int)q->c); break;
    case OP_CLEAR: {
        const PClear *p = (const PClear *)b;
        R->clear(p->mask, p->has_rgba ? p->rgba : NULL, p->depth, p->stencil,
                 p->has_rect ? p->rect : NULL);
        break; }
    case OP_PRESENT:
        R->present(real_of(K_RT, q->a), q->b, q->c);
        if (R->pipeline_ms) g_pipeline_ms = R->pipeline_ms();
        if (R->gpu_ms) { double bz = 0, wt = 0; R->gpu_ms(&bz, &wt); g_gpu_busy = bz; g_gpu_waited = wt; }
        break;
    case OP_REPORT: R->report(); break;
    case OP_VBUF_NEW: map_set(K_VB, q->a, R->vbuf_new((int)q->b)); break;
    case OP_VBUF_FREE:
        R->vbuf_free(real_of(K_VB, q->a));
        map_set(K_VB, q->a, 0);
        break;
    case OP_VBUF_FILL: {
        const PFill *p = (const PFill *)b;
        const void *v, *ix;
        cur = b + al(sizeof *p);
        v = blob_get(&p->v, &cur);
        ix = blob_get(&p->idx, &cur);
        R->vbuf_fill(real_of(K_VB, p->vb), (const float *)v, p->vbytes, ix, p->ibytes);
        blob_done(&p->v); blob_done(&p->idx);
        break; }
    case OP_VBUF_BIND: {
        const PBind *p = (const PBind *)b;
        R->vbuf_bind(real_of(K_VB, p->vb), p->stride, p->attr_at, p->const_mask, p->const_attr);
        break; }
    case OP_CACHE_OPEN: {
        const PCache *p = (const PCache *)b;
        const char *d, *t;
        cur = b + al(sizeof *p);
        d = (const char *)blob_get(&p->dir, &cur);
        t = (const char *)blob_get(&p->tag, &cur);
        R->cache_open(d, t);
        blob_done(&p->dir); blob_done(&p->tag);
        break; }
    case OP_CACHE_IDLE: g_cache_left = R->cache_idle(*(const double *)b); break;
    case OP_SAMPLES_MAX: g_reply = R->samples_max(); break;
    case OP_SAMPLES: g_reply = R->samples((int)q->a); break;
    case OP_ZREV: g_reply = R->zrev((int)q->a); break;
    case OP_RUN: {
        const PRun *p = (const PRun *)b;
        p->fn(b + al(sizeof *p));
        break; }
    case OP_FRAME:
        st(&g_frames_done, ld(&g_frames_done) + 1);
        break;
    }
}

static int SDLCALL worker(void *arg)
{
    (void)arg;
    if (sw_thread_pin) sw_thread_pin("gl", 0, 0);
    if (!g_ctx_take()) { __atomic_store_n(&g_took, -1, __ATOMIC_SEQ_CST); return 0; }
    __atomic_store_n(&g_took, 1, __ATOMIC_SEQ_CST);
    for (;;) {
        uint64_t r = g_r, w;
        int spin = 0;
        while ((w = ld(&g_w)) == r) {
            if (++spin < 2000) continue;
            SDL_LockMutex(g_mu);
            __atomic_store_n(&g_work_sleeps, 1, __ATOMIC_SEQ_CST);
            if (ld(&g_w) == r) SDL_CondWaitTimeout(g_cv_work, g_mu, 5);
            __atomic_store_n(&g_work_sleeps, 0, __ATOMIC_SEQ_CST);
            SDL_UnlockMutex(g_mu);
            spin = 0;
        }
        while (r < w) {
            const Hdr *h = (const Hdr *)(g_ring + r % RING_BYTES);
            uint64_t next = r + sizeof(Hdr) + h->len;
            uint64_t seq = h->seq;
            if (h->op != OP_WRAP) {
                if (split_on()) {
                    /* Two clock reads per entry, on the worker only. It
                     * replays thousands of entries a frame, not the
                     * millions of methods the walk decodes, so the reads
                     * stay a small fraction of the time they measure. */
                    uint32_t op = h->op;
                    double t0 = split_now();
                    int g;
                    run_one(h);
                    g = split_group(op);
                    g_split_ms[g] += split_now() - t0;
                    g_split_n[g]++;
                } else run_one(h);
            }
            r = next;
            st(&g_r, r);
            if (seq) st(&g_done, seq);
            wake_producer();
        }
    }
    return 0;
}

/* ---- for nv2a_gl.c ------------------------------------------------------------- */

int nv2a_gfx_thread_on(void) { return g_on; }

/* One report interval of the split counters, emptied into the caller's
 * arrays (groups in the order of the enum above: draws, binds, uploads,
 * program creation, present, other), with the producer's stall time and
 * the frames the worker finished in the interval. Returns 0, and touches
 * nothing, while RECOMP_GL_SPLIT is off. The worker may be adding to a
 * counter while it is read here; a sample landing either side of the
 * reset is the accepted price of not locking the replay for a
 * diagnostic. */
int nv2a_gfx_thread_split(double ms[6], unsigned long n[6],
                          double *stall_ms, uint64_t *frames)
{
    static uint64_t prev_frames;
    uint64_t fd;
    int i;
    if (!split_on()) return 0;
    for (i = 0; i < SPLIT_GROUPS; i++) {
        ms[i] = g_split_ms[i];
        n[i] = g_split_n[i];
        g_split_ms[i] = 0.0;
        g_split_n[i] = 0;
    }
    *stall_ms = g_split_stall;
    g_split_stall = 0.0;
    fd = ld(&g_frames_done);
    *frames = fd - prev_frames;
    prev_frames = fd;
    return 1;
}

/* The real target behind one of ours, on the worker (inside a run). */
uint32_t nv2a_gfx_thread_real_target(uint32_t rt) { return real_of(K_RT, rt); }

/* Run fn(arg copy) on the worker, in order with the device's calls. */
void nv2a_gfx_thread_run(void (*fn)(const void *arg), const void *arg, uint32_t bytes)
{
    PRun *p = (PRun *)put_begin(OP_RUN, (uint32_t)(al(sizeof *p) + al(bytes)));
    p->fn = fn; p->bytes = bytes;
    if (bytes) memcpy((uint8_t *)p + al(sizeof *p), arg, bytes);
    put_end();
}
/* The same, and wait for it. */
void nv2a_gfx_thread_run_sync(void (*fn)(const void *arg), const void *arg, uint32_t bytes)
{
    PRun *p = (PRun *)put_begin(OP_RUN, (uint32_t)(al(sizeof *p) + al(bytes)));
    p->fn = fn; p->bytes = bytes;
    if (bytes) memcpy((uint8_t *)p + al(sizeof *p), arg, bytes);
    wait_done(put_end());
}

/* The end of a frame: wait while the worker is more than one frame behind. */
void nv2a_gfx_thread_frame(void)
{
    uint64_t in;
    put_begin(OP_FRAME, 0);
    put_end();
    in = ++g_frames_in;
    PROD_WAIT(ld(&g_frames_done) + 1 >= in);
}

/* Start: `real` is the device made on this thread, with its context current
 * here; release() lets go of it on this thread and take() makes it current
 * on the worker. Returns the device to draw through from now on. */
const Nv2aGfx *nv2a_gfx_thread_start(const Nv2aGfx *real, void (*release)(void),
                                     int (*take)(void))
{
    const char *e = getenv("RECOMP_GL_THREAD");
#if defined(__SWITCH__)
    /* On by default on the Switch (run 11: heavy scenes 15-24% faster);
     * RECOMP_GL_THREAD=0 draws on the GPU thread as before. */
    if (e && e[0] == '0') return real;
#else
    if (!(e && e[0] == '1')) return real;
#endif
    if (real->lang == NV2A_LANG_MSL) return real;              /* OpenGL only */
    g_ring = (uint8_t *)malloc(RING_BYTES);
    g_mu = SDL_CreateMutex();
    g_cv_work = SDL_CreateCond();
    g_cv_prod = SDL_CreateCond();
    if (!g_ring || !g_mu || !g_cv_work || !g_cv_prod) {
        fprintf(stderr, "  [GL] no device thread (out of memory): drawing on this thread\n");
        return real;
    }
    R = real;
    { Nv2aSampler s; memset(&s, 0, sizeof s); (void)nv2a_gfx_gl_dxt_ok(1, &s, &s); }  /* S3TC asked here */
    g_dev = *real;
    g_dev.init = NULL;
    g_dev.target_new = t_target_new;       g_dev.target_free = t_target_free;
    g_dev.target_bind = t_target_bind;     g_dev.target_texture = t_target_texture;
    g_dev.target_snapshot = t_target_snapshot;
    g_dev.target_read = t_target_read;
    g_dev.target_read_depth = real->target_read_depth ? t_target_read_depth : NULL;
    g_dev.tex_new = t_tex_new;             g_dev.tex_free = t_tex_free;
    g_dev.tex_read = real->tex_read ? t_tex_read : NULL;
    g_dev.prog_new = t_prog_new;           g_dev.prog_free = t_prog_free;
    g_dev.prog_uses = t_prog_uses;         g_dev.prog_use = t_prog_use;
    g_dev.uniform_f = t_uniform_f;         g_dev.uniform_i = t_uniform_i;
    g_dev.texture = t_texture;             g_dev.state = t_state;
    g_dev.state_invalidate = t_state_invalidate;
    g_dev.vertices = t_vertices;           g_dev.draw = t_draw;
    g_dev.clear = t_clear;                 g_dev.present = t_present;
    g_dev.report = t_report;
    g_dev.vbuf_new = t_vbuf_new;           g_dev.vbuf_free = t_vbuf_free;
    g_dev.vbuf_fill = t_vbuf_fill;         g_dev.vbuf_bind = t_vbuf_bind;
    g_dev.draw_indexed = t_draw_indexed;
    g_dev.uniform_f_part = real->uniform_f_part ? t_uniform_f_part : NULL;
    g_dev.pipeline_ms = real->pipeline_ms ? t_pipeline_ms : NULL;
    g_dev.cache_open = real->cache_open ? t_cache_open : NULL;
    g_dev.cache_idle = real->cache_idle ? t_cache_idle : NULL;
    g_dev.target_smaa = real->target_smaa ? t_target_smaa : NULL;
    g_dev.samples_max = real->samples_max ? t_samples_max : NULL;
    g_dev.samples = real->samples ? t_samples : NULL;
    g_dev.zrev = real->zrev ? t_zrev : NULL;
    g_dev.gpu_ms = real->gpu_ms ? t_gpu_ms : NULL;
    g_dev.tex_new_dxt = real->tex_new_dxt ? t_tex_new_dxt : NULL;
    g_ctx_take = take;
    release();
    if (!SDL_CreateThread(worker, "nv2a-gl", NULL)) {
        take();
        fprintf(stderr, "  [GL] no device thread (%s): drawing on this thread\n", SDL_GetError());
        return real;
    }
    while (!__atomic_load_n(&g_took, __ATOMIC_SEQ_CST)) SDL_Delay(1);
    if (g_took < 0) {
        take();
        fprintf(stderr, "  [GL] no device thread (the context stayed here): drawing on this thread\n");
        return real;
    }
    g_on = 1;
    fprintf(stderr, "  [GL] the device runs on a thread of its own (RECOMP_GL_THREAD=0 for none)\n");
    return &g_dev;
}

#else  /* no SDL: no thread */

int nv2a_gfx_thread_on(void) { return 0; }
int nv2a_gfx_thread_split(double ms[6], unsigned long n[6],
                          double *stall_ms, uint64_t *frames)
{
    (void)ms; (void)n; (void)stall_ms; (void)frames;
    return 0;
}
uint32_t nv2a_gfx_thread_real_target(uint32_t rt) { return rt; }
const Nv2aGfx *nv2a_gfx_thread_start(const Nv2aGfx *real, void (*release)(void),
                                     int (*take)(void))
{
    (void)release; (void)take;
    return real;
}
void nv2a_gfx_thread_frame(void) {}
void nv2a_gfx_thread_run(void (*fn)(const void *arg), const void *arg, uint32_t bytes)
{
    (void)bytes; fn(arg);
}
void nv2a_gfx_thread_run_sync(void (*fn)(const void *arg), const void *arg, uint32_t bytes)
{
    (void)bytes; fn(arg);
}

#endif
