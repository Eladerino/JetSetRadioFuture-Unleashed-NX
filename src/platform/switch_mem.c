/*
 * switch_mem.c -- guest memory on Horizon (Nintendo Switch).
 *
 * The layout (xbox_memory_layout.c) wants what Linux gives it with memfd and
 * MAP_SHARED: one block of guest RAM visible at several addresses at once --
 * the base view, the RAM mirrors, the tiled aperture at 0xF0000000 -- all
 * inside a 4 GB span it reserved up front. Horizon has no mmap, and every
 * mapping object it offers (transfer memory, shared memory, code memory)
 * allows roughly one extra mapping of itself. What it does allow is
 * svcMapProcessMemory of a code-memory window, any number of times:
 *
 *   - A backing is a heap allocation, turned into code-memory windows of at
 *     most 2 MB with svcMapProcessCodeMemory and relaxed to read-write with
 *     svcSetProcessMemoryPermission. From then on the heap pointer itself
 *     must not be touched: the code mapping takes those pages away from the
 *     heap until it is undone.
 *   - A view is svcMapProcessMemory of the windows at the address wanted.
 *     Every view of one backing is the same physical pages.
 *   - A reservation (PROT_NONE) is a libnx virtmem reservation: bookkeeping
 *     that keeps libnx's own allocators out, no syscall.
 *
 * The technique follows the published LostOdysseyNX port; this is its own
 * implementation. It needs the process handle and the syscall hints that
 * hbloader passes to title-takeover launches, and not to applet ones.
 *
 * Everything is aligned to 2 MB: the span, the windows, the heap block. An
 * AArch64 page table can map 2 MB with one block entry instead of 512 page
 * entries when the virtual address and the physical memory are both aligned
 * to it, which shrinks the page tables for ~30 views of 64 MB and lets the
 * TLB cover guest RAM instead of missing on every scattered access. Whether
 * the kernel uses blocks also depends on the heap's physical layout; the
 * alignment is what makes it possible at all.
 *
 * Protection changes on memory this layer owns are not applied. On the
 * other POSIX hosts a decommit is mprotect(PROT_NONE), which keeps the
 * page's contents, and the later commit gets them back intact, so the one
 * thing the protection adds is a fault on a stray access. Horizon gives the
 * process no way to resume from such a fault, and unmapping a view frees no
 * memory (the backing stays allocated), so the pages stay mapped and the
 * call succeeds. That is the same trade the Apple hosts make for
 * protections finer than their 16 KB pages.
 */
#if defined(__SWITCH__)

#include <switch/types.h>
#include <switch/result.h>
#include <switch/kernel/svc.h>
#include <switch/kernel/virtmem.h>
#include <switch/runtime/env.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "switch_mem.h"

#define SW_PAGE      0x1000u
#define SW_BLOCK     0x200000u    /* window size and alignment */
#define SW_MAX_MAPS  320          /* views + commits */
#define SW_MAX_RESV  16

typedef struct {
    void  *heap;        /* never dereferenced once windowed */
    size_t total;
    int    nwin;
    void  *windows[];   /* one per SW_BLOCK slice */
} sw_backing;

/* A view of a file-mapping object's backing, or a commit (which owns its
 * backing and destroys it when unmapped). */
typedef struct {
    void       *dst;
    size_t      len;
    sw_backing *bk;
    size_t      off;
    int         owns_bk;
} sw_map_rec;

typedef struct { void *base; size_t len; VirtmemReservation *rv; } sw_resv_rec;

static sw_map_rec  s_maps[SW_MAX_MAPS];
static sw_resv_rec s_resv[SW_MAX_RESV];
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static Handle s_proc = INVALID_HANDLE;

static unsigned long long s_map_calls, s_backed_bytes;
static int s_said_noaccess;

static Handle sw_proc(void)
{
    if (s_proc == INVALID_HANDLE) {
        if (!envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x74)
            || !envIsSyscallHinted(0x75) || !envIsSyscallHinted(0x77)
            || !envIsSyscallHinted(0x78))
            return INVALID_HANDLE;
        s_proc = envGetOwnProcessHandle();
    }
    return s_proc;
}

int sw_mem_available(void)
{
    return sw_proc() != INVALID_HANDLE;
}

static size_t sw_round_page(size_t len)
{
    return (len + SW_PAGE - 1) & ~(size_t)(SW_PAGE - 1);
}

static uintptr_t sw_align_block(uintptr_t a)
{
    return (a + SW_BLOCK - 1) & ~(uintptr_t)(SW_BLOCK - 1);
}

/* libnx's finders take a guard size, not an alignment: ask for a block
 * more than needed and round up inside it. The caller holds virtmemLock
 * until the range is mapped or reserved. */
static void *sw_find_aslr(size_t len)
{
    void *raw = virtmemFindAslr(len + SW_BLOCK, 0);
    return raw ? (void *)sw_align_block((uintptr_t)raw) : NULL;
}

static void *sw_find_code(size_t len)
{
    void *raw = virtmemFindCodeMemory(len + SW_BLOCK, 0);
    return raw ? (void *)sw_align_block((uintptr_t)raw) : NULL;
}

static size_t sw_window_len(const sw_backing *bk, int i)
{
    size_t off = (size_t)i * SW_BLOCK;
    return bk->total - off < SW_BLOCK ? bk->total - off : SW_BLOCK;
}

/* Undo the windows and give the heap block back. A window that will not
 * unmap still holds its pages, so the block is leaked rather than freed
 * under it. */
static void sw_backing_destroy(sw_backing *bk)
{
    Handle proc = sw_proc();
    int clean = 1;
    for (int i = bk->nwin - 1; i >= 0; i--) {
        size_t off = (size_t)i * SW_BLOCK;
        if (R_FAILED(svcUnmapProcessCodeMemory(proc, (u64)bk->windows[i],
                        (u64)((char *)bk->heap + off), sw_window_len(bk, i))))
            clean = 0;
    }
    if (clean) {
        s_backed_bytes -= bk->total;
        free(bk->heap);
    }
    free(bk);
}

static sw_backing *sw_backing_create(size_t total)
{
    Handle proc = sw_proc();
    if (proc == INVALID_HANDLE || !total) return NULL;
    total = sw_round_page(total);
    int nwin = (int)((total + SW_BLOCK - 1) / SW_BLOCK);
    sw_backing *bk = (sw_backing *)malloc(sizeof(sw_backing)
                                          + (size_t)nwin * sizeof(void *));
    if (!bk) return NULL;
    bk->heap  = memalign(total >= SW_BLOCK ? SW_BLOCK : SW_PAGE, total);
    bk->total = total;
    bk->nwin  = 0;
    if (!bk->heap) { free(bk); return NULL; }
    memset(bk->heap, 0, total);
    s_backed_bytes += total;

    for (int i = 0; i < nwin; i++) {
        size_t len = sw_window_len(bk, i);
        u64 src = (u64)((char *)bk->heap + (size_t)i * SW_BLOCK);
        Result rc;
        virtmemLock();
        void *window = sw_find_code(len);
        rc = window ? svcMapProcessCodeMemory(proc, (u64)window, src, len)
                    : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        if (R_SUCCEEDED(rc)) {
            rc = svcSetProcessMemoryPermission(proc, (u64)window, len, Perm_Rw);
            if (R_FAILED(rc))
                svcUnmapProcessCodeMemory(proc, (u64)window, src, len);
        }
        virtmemUnlock();
        if (R_FAILED(rc)) {
            fprintf(stderr, "  [SWMEM] code-memory window %d of %d failed"
                            " (rc 0x%X)\n", i, nwin, (unsigned)rc);
            sw_backing_destroy(bk);
            return NULL;
        }
        bk->windows[bk->nwin++] = window;
    }
    return bk;
}

static void sw_backing_unmap(sw_backing *bk, size_t off, void *dst, size_t len)
{
    Handle proc = sw_proc();
    size_t pos = off, end = off + len;
    char *d = (char *)dst;
    while (pos < end) {
        int wi = (int)(pos / SW_BLOCK);
        if (wi >= bk->nwin) return;
        size_t woff = pos - (size_t)wi * SW_BLOCK;
        size_t seg = sw_window_len(bk, wi) - woff;
        if (seg > end - pos) seg = end - pos;
        svcUnmapProcessMemory(d, proc, (u64)((char *)bk->windows[wi] + woff), seg);
        d += seg;
        pos += seg;
    }
}

/* Map [off, off+len) of bk at dst, one svcMapProcessMemory per window it
 * crosses; on failure, take back what was mapped. Caller holds
 * virtmemLock. */
static int sw_backing_map(sw_backing *bk, size_t off, void *dst, size_t len)
{
    Handle proc = sw_proc();
    size_t pos = off, end = off + len;
    char *d = (char *)dst;
    if (proc == INVALID_HANDLE || end > bk->total) return -1;
    while (pos < end) {
        int wi = (int)(pos / SW_BLOCK);
        size_t woff = pos - (size_t)wi * SW_BLOCK;
        size_t seg = sw_window_len(bk, wi) - woff;
        if (seg > end - pos) seg = end - pos;
        Result rc = svcMapProcessMemory(d, proc,
                                        (u64)((char *)bk->windows[wi] + woff), seg);
        s_map_calls++;
        if (R_FAILED(rc)) {
            if (pos > off) sw_backing_unmap(bk, off, dst, pos - off);
            fprintf(stderr, "  [SWMEM] view at %p (+0x%zx) failed (rc 0x%X)\n",
                    dst, pos - off, (unsigned)rc);
            return -1;
        }
        d += seg;
        pos += seg;
    }
    return 0;
}

static sw_map_rec *sw_free_rec(void)
{
    for (int i = 0; i < SW_MAX_MAPS; i++)
        if (!s_maps[i].dst) return &s_maps[i];
    return NULL;
}

static sw_map_rec *sw_rec_containing(uintptr_t a)
{
    for (int i = 0; i < SW_MAX_MAPS; i++) {
        uintptr_t b = (uintptr_t)s_maps[i].dst;
        if (b && a >= b && a < b + s_maps[i].len) return &s_maps[i];
    }
    return NULL;
}

/* Map len bytes of bk (from off) at dst, or at a found address when dst is
 * NULL, and record it. Caller holds s_lock. */
static void *sw_place(sw_backing *bk, size_t off, size_t len, void *dst,
                      int owns_bk)
{
    sw_map_rec *r = sw_free_rec();
    if (!r) return NULL;
    virtmemLock();
    void *d = dst ? dst : sw_find_aslr(len);
    int ok = d && sw_backing_map(bk, off, d, len) == 0;
    virtmemUnlock();
    if (!ok) return NULL;
    r->dst = d;
    r->len = len;
    r->bk  = bk;
    r->off = off;
    r->owns_bk = owns_bk;
    return d;
}

static void *sw_reserve(void *addr, size_t len)
{
    sw_resv_rec *slot = NULL;
    for (int i = 0; i < SW_MAX_RESV && !slot; i++)
        if (!s_resv[i].base) slot = &s_resv[i];
    if (!slot) return MAP_FAILED;
    virtmemLock();
    if (!addr) addr = sw_find_aslr(len);
    VirtmemReservation *rv = addr ? virtmemAddReservation(addr, len) : NULL;
    virtmemUnlock();
    if (!rv) return MAP_FAILED;
    slot->base = addr;
    slot->len  = len;
    slot->rv   = rv;
    return addr;
}

static void *sw_commit(void *addr, size_t len)
{
    sw_backing *bk = sw_backing_create(len);
    if (!bk) return MAP_FAILED;
    void *d = sw_place(bk, 0, bk->total, addr, 1);
    if (!d) {
        sw_backing_destroy(bk);
        return MAP_FAILED;
    }
    return d;
}

/* A commit at a fixed address may cover memory an earlier commit backs
 * already (VirtualAlloc reaches here whenever the permission call refused
 * any part of the range). POSIX MAP_FIXED would replace that memory;
 * Horizon refuses to map over it. Neither is what a Win32 commit means,
 * which is "make this usable, keep what is there": so back the gaps only. */
static void *sw_commit_gaps(void *addr, size_t len)
{
    uintptr_t p = (uintptr_t)addr, hi = p + len;
    while (p < hi) {
        sw_map_rec *r = sw_rec_containing(p);
        if (r) {
            p = (uintptr_t)r->dst + r->len;
            continue;
        }
        uintptr_t gap_end = hi;
        for (int i = 0; i < SW_MAX_MAPS; i++) {
            uintptr_t b = (uintptr_t)s_maps[i].dst;
            if (b && b > p && b < gap_end) gap_end = b;
        }
        if (sw_commit((void *)p, gap_end - p) == MAP_FAILED)
            return MAP_FAILED;
        p = gap_end;
    }
    return addr;
}

void *sw_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    (void)flags; (void)off;
    /* File views come through sw_map_view, which has the mapping object;
     * a bare fd carries too little. */
    if (!len || fd >= 0 || sw_proc() == INVALID_HANDLE) return MAP_FAILED;
    len = sw_round_page(len);
    pthread_mutex_lock(&s_lock);
    void *r = prot == PROT_NONE ? sw_reserve(addr, len)
            : addr ? sw_commit_gaps(addr, len) : sw_commit(NULL, len);
    pthread_mutex_unlock(&s_lock);
    return r;
}

int sw_munmap(void *addr, size_t len)
{
    int rc = -1;
    (void)len;
    pthread_mutex_lock(&s_lock);
    for (int i = 0; i < SW_MAX_MAPS; i++) {
        sw_map_rec *r = &s_maps[i];
        if (r->dst != addr) continue;
        sw_backing_unmap(r->bk, r->off, r->dst, r->len);
        if (r->owns_bk) sw_backing_destroy(r->bk);
        memset(r, 0, sizeof *r);
        rc = 0;
        goto out;
    }
    for (int i = 0; i < SW_MAX_RESV; i++) {
        if (s_resv[i].base != addr) continue;
        virtmemLock();
        virtmemRemoveReservation(s_resv[i].rv);
        virtmemUnlock();
        memset(&s_resv[i], 0, sizeof s_resv[i]);
        rc = 0;
        goto out;
    }
out:
    pthread_mutex_unlock(&s_lock);
    return rc;
}

int sw_mprotect(void *addr, size_t len, int prot)
{
    Handle proc = sw_proc();
    if (proc == INVALID_HANDLE || !len) return -1;
    uintptr_t p  = (uintptr_t)addr & ~(uintptr_t)(SW_PAGE - 1);
    uintptr_t hi = ((uintptr_t)addr + len + SW_PAGE - 1) & ~(uintptr_t)(SW_PAGE - 1);
    int rc = 0;
    pthread_mutex_lock(&s_lock);
    while (p < hi) {
        sw_map_rec *r = sw_rec_containing(p);
        if (r) {
            /* Owned: left as it is (see the top of this file). */
            uintptr_t end = (uintptr_t)r->dst + r->len;
            if (!(prot & (PROT_READ | PROT_WRITE)) && !s_said_noaccess++)
                fprintf(stderr, "  [SWMEM] taking access away from guest memory"
                                " is not applied on Switch: the pages stay"
                                " mapped, contents kept\n");
            p = end < hi ? end : hi;
            continue;
        }
        /* Not ours (or reserved and not backed yet), up to the next range
         * that is: one permission SVC for the run. Its refusal is what
         * tells VirtualAlloc to make a fresh commit there instead. */
        uintptr_t run = hi;
        for (int i = 0; i < SW_MAX_MAPS; i++) {
            uintptr_t b = (uintptr_t)s_maps[i].dst;
            if (b && b > p && b < run) run = b;
        }
        u32 perm = (prot & PROT_WRITE) ? Perm_Rw
                 : (prot & PROT_READ)  ? Perm_R : Perm_None;
        if (R_FAILED(svcSetProcessMemoryPermission(proc, (u64)p, run - p, perm))) {
            rc = -1;
            break;
        }
        p = run;
    }
    pthread_mutex_unlock(&s_lock);
    return rc;
}

void *sw_map_view(void **backing, size_t map_size, size_t off, size_t len,
                  void *dst)
{
    void *d = NULL;
    if (sw_proc() == INVALID_HANDLE || !len || off + len > map_size) return NULL;
    len = sw_round_page(len);
    pthread_mutex_lock(&s_lock);
    if (!*backing) *backing = sw_backing_create(map_size);
    if (*backing) d = sw_place((sw_backing *)*backing, off, len, dst, 0);
    pthread_mutex_unlock(&s_lock);
    return d;
}

void sw_mem_log_summary(void)
{
    int maps = 0, resv = 0;
    pthread_mutex_lock(&s_lock);
    for (int i = 0; i < SW_MAX_MAPS; i++) maps += s_maps[i].dst != NULL;
    for (int i = 0; i < SW_MAX_RESV; i++) resv += s_resv[i].base != NULL;
    fprintf(stderr, "  [SWMEM] %d views and commits, %d reservations, %llu MB"
                    " backed, %llu map calls\n", maps, resv,
            s_backed_bytes >> 20, s_map_calls);
    pthread_mutex_unlock(&s_lock);
}

#endif /* __SWITCH__ */
