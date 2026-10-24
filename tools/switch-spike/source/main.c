/*
 * switch-spike v8 -- the engine's guest-memory layer at full scale.
 *
 * v1-v7 (Muse's spike) proved the mechanism at 1 MB with two extra views.
 * This one runs the real code, src/platform/switch_mem.c compiled in as it
 * is, through the shape xbox_memory_layout.c asks for:
 *
 *   1. a 4 GB PROT_NONE reservation
 *   2. one 64 MB backing, viewed at the base, at 28 mirrors 64 MB apart,
 *      and at the tiled aperture (+0xF0000000)   -- ~960 map syscalls
 *   3. every view checked as an alias of the base, at its first page, at a
 *      2 MB window seam, and at its last word
 *   4. mprotect on owned memory: succeeds, and changes nothing
 *   5. VirtualAlloc's commit path inside the reservation: the permission
 *      call refuses unbacked space, then a fixed commit lands there
 *   6. unmapping one mirror and mapping it again
 *   7. a small view at an unaligned offset across a window seam
 *   8. a 64 MB write and read through the base and through a mirror, timed
 *      (block mappings, if the kernel used them, show up here)
 *   9. teardown, and 64 MB of heap afterwards (the backing really freed)
 *
 * Every line goes to the screen and to sdmc:/switch/jsrf-spike8.log.
 * Title takeover only (hold R on a game); applet mode stops at step 0.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "switch_mem.c"

#define MB        (1ull << 20)
#define RAM       (64 * MB)
#define MIRRORS   28
#define TILED_OFF 0xF0000000ull

static FILE *g_log;
static int g_fail;

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    consoleUpdate(NULL);
    if (g_log) fprintf(g_log, "%s\n", buf);
}

static void check(int ok, const char *what)
{
    if (!ok) g_fail++;
    say("  %s  %s", ok ? "ok  " : "FAIL", what);
}

static u64 now_us(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000;
}

static void mem_line(const char *tag)
{
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    say("  memory %s: %llu of %llu MB used", tag,
        (unsigned long long)(used / MB), (unsigned long long)(total / MB));
}

/* Write through v at off, read through the base at off, and the other
 * way, with a value unique to the view and the spot. */
static int alias_at(volatile u8 *base, volatile u8 *v, size_t off, u32 tag)
{
    volatile u32 *a = (volatile u32 *)(base + off);
    volatile u32 *b = (volatile u32 *)(v + off);
    *b = 0x5A000000u ^ tag;
    if (*a != (0x5A000000u ^ tag)) return 0;
    *a = 0xA5000000u ^ tag;
    return *b == (0xA5000000u ^ tag);
}

static int alias_view(volatile u8 *base, volatile u8 *v, u32 tag)
{
    return alias_at(base, v, 0, tag << 8)
        && alias_at(base, v, SW_BLOCK - 4, (tag << 8) | 1)   /* seam */
        && alias_at(base, v, SW_BLOCK, (tag << 8) | 2)
        && alias_at(base, v, RAM - 4, (tag << 8) | 3);
}

static void run(void)
{
    u64 t;

    say("0. launch mode");
    say("  hints 73 %d, 74 %d, 75 %d, 77 %d, 78 %d",
        envIsSyscallHinted(0x73), envIsSyscallHinted(0x74),
        envIsSyscallHinted(0x75), envIsSyscallHinted(0x77),
        envIsSyscallHinted(0x78));
    check(sw_mem_available(), "process-memory syscalls and handle");
    if (!sw_mem_available()) {
        say("  stopping: start this through title takeover (hold R on a game)");
        return;
    }
    mem_line("at start");

    say("1. 4 GB reservation");
    u8 *base = (u8 *)sw_mmap(NULL, 4ull << 30, PROT_NONE, MAP_PRIVATE, -1, 0);
    check(base != MAP_FAILED, "reserved");
    if (base == MAP_FAILED) return;
    say("  base %p (2 MB aligned: %s)", base,
        ((uintptr_t)base & (SW_BLOCK - 1)) ? "no" : "yes");

    say("2. backing and views");
    void *bk = NULL;
    u8 *views[MIRRORS + 2] = { 0 };
    t = now_us();
    views[0] = (u8 *)sw_map_view(&bk, RAM, 0, RAM, base);
    say("  backing + base view: %llu us", (unsigned long long)(now_us() - t));
    check(views[0] == base, "base view at the base");
    if (views[0] != base) return;
    mem_line("with the backing");
    t = now_us();
    int mapped = 0;
    for (int m = 1; m <= MIRRORS; m++) {
        views[m] = (u8 *)sw_map_view(&bk, RAM, 0, RAM, base + (u64)m * RAM);
        mapped += views[m] != NULL;
    }
    views[MIRRORS + 1] = (u8 *)sw_map_view(&bk, RAM, 0, RAM, base + TILED_OFF);
    mapped += views[MIRRORS + 1] != NULL;
    u64 dt = now_us() - t;
    say("  %d of %d further views in %llu us (%llu us each)", mapped,
        MIRRORS + 1, (unsigned long long)dt,
        (unsigned long long)(dt / (MIRRORS + 1)));
    check(mapped == MIRRORS + 1, "all mirrors and the aperture mapped");
    mem_line("with all views");
    sw_mem_log_summary();
    {
        MemoryInfo mi;
        u32 pi;
        if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, (u64)base)))
            say("  base region: addr %llx size %llx type %x perm %x attr %x",
                (unsigned long long)mi.addr, (unsigned long long)mi.size,
                mi.type, mi.perm, mi.attr);
    }

    say("3. aliasing");
    int good = 0;
    for (int m = 1; m <= MIRRORS + 1; m++)
        if (views[m]) good += alias_view(base, views[m], (u32)m);
    say("  %d of %d views alias the base at start, seam and end", good, mapped);
    check(good == mapped, "every view is the same memory");

    say("4. mprotect on owned memory");
    base[0x3000] = 0x77;
    check(sw_mprotect(base + 0x3000, 0x1000, PROT_NONE) == 0, "PROT_NONE accepted");
    check(base[0x3000] == 0x77, "page still readable, contents kept");
    check(sw_mprotect(base + 0x3000, 0x1000, PROT_READ | PROT_WRITE) == 0,
          "PROT_READ|WRITE accepted");

    say("5. commit inside the reservation (VirtualAlloc's path)");
    u8 *contig = base + 0x80000000ull;
    check(sw_mprotect(contig, RAM, PROT_READ | PROT_WRITE) != 0,
          "permission call refuses unbacked space");
    t = now_us();
    u8 *c = (u8 *)sw_mmap(contig, RAM, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_FIXED, -1, 0);
    say("  64 MB commit: %llu us", (unsigned long long)(now_us() - t));
    check(c == contig, "commit at the asked address");
    if (c == contig) {
        c[0] = 1; c[RAM - 1] = 2;
        check(c[0] == 1 && c[RAM - 1] == 2, "commit readable and writable");
        check(sw_mprotect(c, RAM, PROT_READ | PROT_WRITE) == 0,
              "second commit of the same range is a no-op");
        /* A wider commit over it: only the gaps either side get backed. */
        u8 *w = (u8 *)sw_mmap(c - SW_BLOCK, RAM + 2 * SW_BLOCK,
                              PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, -1, 0);
        check(w == c - SW_BLOCK, "overlapping commit accepted");
        if (w == c - SW_BLOCK) {
            w[0] = 3; w[RAM + 2 * SW_BLOCK - 1] = 4;
            check(c[0] == 1 && c[RAM - 1] == 2 && w[0] == 3
                  && w[RAM + 2 * SW_BLOCK - 1] == 4,
                  "old contents kept, new edges usable");
            sw_munmap(w, SW_BLOCK);
            sw_munmap(c + RAM, SW_BLOCK);
        }
    }

    say("6. unmap a mirror and map it again");
    u8 *m5 = base + 5 * RAM;
    check(sw_munmap(m5, RAM) == 0, "mirror 5 unmapped");
    views[5] = (u8 *)sw_map_view(&bk, RAM, 0, RAM, m5);
    check(views[5] == m5, "mirror 5 mapped again at the same address");
    if (views[5]) check(alias_view(base, views[5], 0x55), "and aliases again");

    say("7. small view across a window seam");
    u8 *sv = (u8 *)sw_map_view(&bk, RAM, SW_BLOCK - 0x1000, 0x2000, NULL);
    check(sv != NULL, "8 KB view at offset 2 MB - 4 KB, address chosen");
    if (sv) {
        sv[0] = 0x31; sv[0x1000] = 0x32;
        check(base[SW_BLOCK - 0x1000] == 0x31 && base[SW_BLOCK] == 0x32,
              "both halves land in the right windows");
        check(sw_munmap(sv, 0x2000) == 0, "small view unmapped");
    }

    say("8. 64 MB write and read, timed");
    {
        u8 *targets[2] = { base, base + 17 * RAM };
        const char *names[2] = { "base", "mirror 17" };
        for (int i = 0; i < 2; i++) {
            volatile u64 *p = (volatile u64 *)targets[i];
            u64 sum = 0;
            t = now_us();
            for (size_t k = 0; k < RAM / 8; k++) p[k] = k + ((u64)i << 32);
            u64 tw = now_us() - t;
            t = now_us();
            for (size_t k = 0; k < RAM / 8; k += 512) sum += p[k];  /* 4 KB stride */
            u64 tr = now_us() - t;
            say("  %s: write %llu us (%llu MB/s), 4 KB-stride read %llu us",
                names[i], (unsigned long long)tw,
                (unsigned long long)(tw ? RAM * 1000000ull / MB / tw : 0),
                (unsigned long long)tr);
            (void)sum;
        }
        check(((volatile u64 *)base)[12345] == 12345 + (1ull << 32),
              "mirror write visible at base");
    }

    say("9. teardown");
    mem_line("before");
    int freed = 0;
    for (int m = 0; m <= MIRRORS + 1; m++)
        if (views[m]) freed += sw_munmap(views[m], RAM) == 0;
    say("  %d views unmapped", freed);
    if (c == contig) check(sw_munmap(c, RAM) == 0, "commit unmapped (backing freed)");
    check(sw_munmap(base, 4ull << 30) == 0, "reservation released");
    /* The views' backing has no owner to free it here: the engine keeps
     * it for the life of the mapping object, and so does this. */
    mem_line("after");
    void *h = malloc(RAM);
    check(h != NULL, "64 MB of heap available afterwards");
    free(h);
}

int main(void)
{
    consoleInit(NULL);
    /* switch_mem.c reports to stderr, so stderr is the log. */
    g_log = freopen("sdmc:/switch/jsrf-spike8.log", "w", stderr);
    if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
    say("JSRF guest-memory spike v8 (the engine's switch_mem.c)");

    run();
    say(g_fail ? "RESULT: %d check(s) FAILED" : "RESULT: all checks passed", g_fail);
    say("Press + to exit. Log: sdmc:/switch/jsrf-spike8.log");
    g_log = NULL;
    fflush(stderr);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        svcSleepThread(20000000ULL);
    }
    consoleExit(NULL);
    return 0;
}
