#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32; typedef uint64_t u64; typedef u32 Result; typedef u32 Handle;
/* Heap blocks are memfds so a "code window" over one aliases it. */
typedef struct { uintptr_t lo, hi; int fd; } blk;      /* heap block */
typedef struct { uintptr_t lo, hi; int fd; size_t off; } win;  /* window, or view */
static blk B[64]; static win W[512], V[2048];
static pthread_mutex_t vm = PTHREAD_MUTEX_INITIALIZER;
int mock_heap_live, mock_svc_calls;
bool envIsSyscallHinted(unsigned s) { (void)s; return getenv("MOCK_APPLET") == NULL; }
Handle envGetOwnProcessHandle(void) { return 0x1234; }
void virtmemLock(void) { pthread_mutex_lock(&vm); }
void virtmemUnlock(void) { pthread_mutex_unlock(&vm); }
/* Ask the kernel for free space, give it back: free until someone maps it. */
static void *find(size_t n) {
    void *p = mmap(NULL, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    munmap(p, n); return p;
}
void *virtmemFindAslr(size_t n, size_t g) { (void)g; return find(n); }
void *virtmemFindCodeMemory(size_t n, size_t g) { (void)g; return find(n); }
/* A reservation holds a PROT_NONE placeholder so find() cannot return it;
 * MAP_FIXED views replace pages inside it, which is how Horizon behaves for
 * a bookkeeping-only reservation. */
typedef struct VirtmemReservation { void *p; size_t n; } VirtmemReservation;
VirtmemReservation *virtmemAddReservation(void *m, size_t n) {
    if (mmap(m, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0) != m) return NULL;
    VirtmemReservation *r = malloc(sizeof *r); r->p = m; r->n = n; return r;
}
void virtmemRemoveReservation(VirtmemReservation *r) { munmap(r->p, r->n); free(r); }
void *mock_memalign(size_t a, size_t n) {
    int fd = memfd_create("heap", 0);
    if (fd < 0 || ftruncate(fd, n)) return NULL;
    size_t span = n + a;
    char *raw = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uintptr_t lo = ((uintptr_t)raw + a - 1) & ~(uintptr_t)(a - 1);
    munmap(raw, span);
    if (mmap((void *)lo, n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != (void *)lo) return NULL;
    for (int i = 0; i < 64; i++) if (!B[i].lo) { B[i] = (blk){ lo, lo + n, fd }; mock_heap_live++; return (void *)lo; }
    return NULL;
}
void mock_free(void *p) {
    if (!p) return;
    for (int i = 0; i < 64; i++) if (B[i].lo == (uintptr_t)p) {
        munmap(p, B[i].hi - B[i].lo); close(B[i].fd); B[i].lo = 0; mock_heap_live--; return; }
    free(p);
}
static int mapped_any(uintptr_t a, size_t n) {   /* is any page of [a,a+n) mapped by us? */
    for (int i = 0; i < 512; i++) if (W[i].lo && a < W[i].hi && W[i].lo < a + n) return 1;
    for (int i = 0; i < 2048; i++) if (V[i].lo && a < V[i].hi && V[i].lo < a + n) return 1;
    return 0;
}
static int inject(const char *k);
Result svcMapProcessCodeMemory(Handle p, u64 dst, u64 src, u64 n) {
    mock_svc_calls++;
    if (inject("CODE_FAIL")) return 0xBAD2;
    if (dst & 0xFFF || mapped_any(dst, n)) return 0xD401;
    for (int i = 0; i < 64; i++) if (B[i].lo && src >= B[i].lo && src + n <= B[i].hi) {
        size_t off = src - B[i].lo;
        if (mmap((void *)dst, n, PROT_READ, MAP_SHARED | MAP_FIXED_NOREPLACE, B[i].fd, off) != (void *)dst) return 0xD401;
        mprotect((void *)src, n, PROT_NONE);            /* the heap pages are taken away */
        for (int j = 0; j < 512; j++) if (!W[j].lo) { W[j] = (win){ dst, dst + n, B[i].fd, off }; return 0; }
    }
    return 0xCA01;
}
Result svcUnmapProcessCodeMemory(Handle p, u64 dst, u64 src, u64 n) {
    mock_svc_calls++;
    for (int j = 0; j < 512; j++) if (W[j].lo == dst && W[j].hi == dst + n) {
        for (int k = 0; k < 2048; k++)            /* Horizon refuses while views exist */
            if (V[k].lo && V[k].fd == W[j].fd && V[k].off < W[j].off + n && W[j].off < V[k].off + (V[k].hi - V[k].lo)) return 0xE801;
        munmap((void *)dst, n); mprotect((void *)src, n, PROT_READ | PROT_WRITE); W[j].lo = 0; return 0; }
    return 0xCC01;
}
Result svcSetProcessMemoryPermission(Handle p, u64 a, u64 n, u32 perm) {
    mock_svc_calls++;
    for (int j = 0; j < 512; j++) if (W[j].lo <= a && a + n <= W[j].hi) {
        int pr = (perm & 1 ? PROT_READ : 0) | (perm & 2 ? PROT_WRITE : 0);
        return mprotect((void *)a, n, pr) ? 0xCC01 : 0; }
    return 0xD401;    /* not code memory: refused */
}
static int inject(const char *k) { static int n[2]; int i = k[0] == 'V'; const char *e = getenv(k); return e && ++n[i] == atoi(e); }
Result svcMapProcessMemory(void *dst, Handle p, u64 src, u64 n) {
    mock_svc_calls++;
    if (inject("VIEW_FAIL")) return 0xBAD1;
    if (n > 0x200000) return 0xE01;   /* keep callers honest about per-window calls */
    for (int j = 0; j < 512; j++) if (W[j].lo <= src && src + n <= W[j].hi) {
        size_t off = W[j].off + (src - W[j].lo);
        if (mapped_any((uintptr_t)dst, n)) return 0xD401;
        /* MAP_FIXED: may land inside a reservation placeholder (ours). */
        if (mmap(dst, n, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, W[j].fd, off) != dst) return 0xD401;
        for (int k = 0; k < 2048; k++) if (!V[k].lo) { V[k] = (win){ (uintptr_t)dst, (uintptr_t)dst + n, W[j].fd, off }; return 0; }
    }
    return 0xCC01;
}
Result svcUnmapProcessMemory(void *dst, Handle p, u64 src, u64 n) {
    mock_svc_calls++;
    for (int k = 0; k < 2048; k++) if (V[k].lo == (uintptr_t)dst && V[k].hi == (uintptr_t)dst + n) {
        /* back to an inaccessible placeholder, as a reservation would read */
        mmap(dst, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
        V[k].lo = 0; return 0; }
    return 0xCC01;
}
Result svcQueryMemory(void *mi_, u32 *pi, u64 a) { (void)pi; memset(mi_, 0, 40); ((u64 *)mi_)[0] = a & ~0xFFFull; ((u64 *)mi_)[1] = 0x1000; ((u32 *)mi_)[4] = 1; ((u32 *)mi_)[6] = 3; return 0; }
Result svcGetInfo(u64 *o, u32 a, Handle h, u64 b) { *o = a == 6 ? 3200ull << 20 : (u64)mock_heap_live << 26; return 0; }
u64 armGetSystemTick(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
u64 armTicksToNs(u64 t) { return t; }
void consoleInit(void *c) {} void consoleUpdate(void *c) {} void consoleExit(void *c) {}
