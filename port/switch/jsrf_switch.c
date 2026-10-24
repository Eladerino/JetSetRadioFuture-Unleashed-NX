/*
 * jsrf_switch.c -- the engine inside the Switch .nro.
 *
 * hbmenu (title takeover) loads the .nro and calls its main (main.c),
 * which comes here. The layout on the SD card:
 *
 *   sdmc:/switch/jsrf-unleashed/
 *     jsrf-unleashed.nro      this program
 *     game/                   default.xbe and Media/, or one .iso the
 *                             first launch unpacks into the folder
 *     save/                   the cache and the disk images
 *     settings                key=value lines, as on the other launchers
 *     Logs/last-run.log       this run's log (previous-run.log before it)
 *     tmp/                    TMPDIR (the platform layer's temp files)
 *
 * What the desktop launchers and the Android glue do before the engine
 * starts is done here: the environment (as the binary is loaded), the
 * log, the folders, the disc-image unpack. Then host_main runs on a
 * thread with a 16 MB stack.
 *
 * Launch must be title takeover (hold R while starting a game in
 * hbmenu). Applet mode does not hint the process-memory syscalls the
 * guest memory is built on, and its memory pool is too small besides;
 * that is checked first and said on screen, in plain words, rather
 * than failing later inside the engine.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <switch.h>
#include "xiso.h"
#include "../xbe_check.h"

#ifndef JSRF_VERSION
#define JSRF_VERSION "dev"
#endif

/* libnx configuration, overridden by giving the weak symbols strong
 * definitions here (the same route the other Switch recomp ports
 * take):
 *
 * - AppletType_Application: only the starting value. Under hbloader,
 *   envSetup replaces it with the type the loader reports, so this
 *   matters only for a loader that reports none.
 * - Heap size 0: libnx sizes the heap itself; the engine's guest
 *   backing and its own allocations want everything available.
 * - The nv services transfer-memory area raised from the 8 MiB
 *   default: the GL driver spends it per texture record, and a
 *   texture-heavy title exhausts 8 MiB after a few thousand creates.
 * - A 16 KiB exception stack, so a fault report is not itself
 *   limited by the small default.
 *
 * The types match libnx's own weak definitions exactly: the applet
 * type and the transfer-memory size are 32-bit; the heap size and
 * the exception stack size are 64-bit. A narrower definition here
 * would leave the runtime reading the neighbouring bytes as the
 * value's high half. */
u32 __nx_applet_type = AppletType_Application;
u64 __nx_heap_size = 0;
u32 __nx_nv_transfermem_size = 32u << 20;
__attribute__((aligned(16))) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

#define SWITCH_BASE "sdmc:/switch/jsrf-unleashed"

/* ---- crash report ---------------------------------------------------
 *
 * Horizon has no signals, so the engine's crash handler is not built
 * here; a fault comes to libnx's exception handler instead, on the
 * exception stack above, and libnx stops the process when it returns.
 * This writes what a fault needs into the log first: where (pc and lr,
 * also as offsets into this .nro, which is what addr2line and a
 * disassembly of the .elf take), what was touched (far, and as a guest
 * address when it falls inside the guest's 4 GB), the registers, and
 * the frame-pointer chain.
 *
 * The fault may have been inside the C library holding a lock, so
 * nothing here goes through stdio or malloc: digits are formatted by
 * hand and written to the log's descriptor directly. A frame pointer
 * is only followed after svcQueryMemory says its page is mapped and
 * readable; a second fault in here would end the report. */
extern char __start__[];
extern ptrdiff_t xbox_GetMemoryOffset(void);

static int s_crash_fd = 2;

static void crash_puts(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    while (n) {
        ssize_t w = write(s_crash_fd, s, n);
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

static void crash_hex(const char *label, u64 v)
{
    char b[48];
    int i = 0, d;
    while (label[i] && i < 24) { b[i] = label[i]; i++; }
    b[i++] = '0'; b[i++] = 'x';
    for (d = 60; d >= 0; d -= 4) b[i++] = "0123456789abcdef"[(v >> d) & 0xF];
    b[i] = 0;
    crash_puts(b);
}

/* "  pc 0x...  (nro+0x...)" when the address is inside this module. */
static void crash_code(const char *label, u64 a)
{
    u64 base = (u64)(uintptr_t)__start__;
    MemoryInfo mi;
    u32 pi;
    crash_hex(label, a);
    if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, base)) && a >= base
        && a < base + 0x10000000ull)   /* a generous bound on .nro size */
        crash_hex("  (nro+", a - base), crash_puts(")");
    crash_puts("\n");
}

static int crash_readable(u64 a)
{
    MemoryInfo mi;
    u32 pi;
    if (a & 0xF) return 0;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) return 0;
    return (mi.perm & Perm_R) && (mi.type & MemState_Type) != MemType_Unmapped
           && a + 16 <= mi.addr + mi.size;
}

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    static const struct { u32 code; const char *name; } k_desc[] = {
        { ThreadExceptionDesc_InstructionAbort, "instruction abort" },
        { ThreadExceptionDesc_MisalignedPC, "misaligned pc" },
        { ThreadExceptionDesc_MisalignedSP, "misaligned sp" },
        { ThreadExceptionDesc_SError, "SError" },
        { ThreadExceptionDesc_BadSVC, "bad SVC" },
        { ThreadExceptionDesc_Trap, "trap" },
        { ThreadExceptionDesc_Other, "data abort or other" },
    };
    const char *what = "unknown";
    size_t i;
    for (i = 0; i < sizeof k_desc / sizeof k_desc[0]; i++)
        if (k_desc[i].code == ctx->error_desc) what = k_desc[i].name;

    crash_puts("\n[CRASH] ");
    crash_puts(what);
    crash_hex(" (desc ", ctx->error_desc);
    crash_hex(", esr ", ctx->esr);
    crash_puts(")\n");
    crash_code("[CRASH] pc ", ctx->pc.x);
    crash_code("[CRASH] lr ", ctx->lr.x);
    crash_hex("[CRASH] sp ", ctx->sp.x);
    crash_hex("  fp ", ctx->fp.x);
    crash_puts("\n");
    crash_hex("[CRASH] far ", ctx->far.x);
    {
        u64 g = ctx->far.x - (u64)xbox_GetMemoryOffset();
        if (xbox_GetMemoryOffset() && g < 0x100000000ull)
            crash_hex("  = guest ", g);
    }
    crash_puts("\n");
    for (i = 0; i < 29; i++) {
        char l[12] = "[CRASH] x";
        l[9] = (char)('0' + i / 10);
        l[10] = (char)('0' + i % 10);
        l[11] = 0;
        crash_hex(l, ctx->cpu_gprs[i].x);
        crash_puts(i % 2 ? "\n" : "  ");
    }
    crash_puts("\n[CRASH] frames (from fp):\n");
    {
        u64 fp = ctx->fp.x;
        for (i = 0; i < 32 && crash_readable(fp); i++) {
            const u64 *f = (const u64 *)(uintptr_t)fp;
            if (!f[1]) break;
            crash_code("[CRASH]   ", f[1]);
            if (f[0] <= fp) break;   /* the chain only grows upward */
            fp = f[0];
        }
    }
    crash_puts("[CRASH] end of report\n");
}

static void mkdir_p(const char *path)
{
    char buf[1024];
    char *p;
    snprintf(buf, sizeof buf, "%s", path);
    for (p = buf + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
    mkdir(buf, 0755);
}

/* Logs/last-run.log, the one before it kept as previous-run.log. One
 * open file behind both streams: Horizon will not open a file for writing
 * a second time while the first handle is open, so the earlier version's
 * two freopen calls left stderr -- everything the engine and the crash
 * report write -- going nowhere, and the log held only stdout. stdout now
 * shares stderr's descriptor (and its file offset), unbuffered, so a crash
 * keeps everything up to the fault, in order.
 *
 * rename() on Horizon refuses an existing destination, hence the remove. */
static void open_log(const char *data)
{
    char logs[1024], last[1100], prev[1100];
    snprintf(logs, sizeof logs, "%s/Logs", data);
    mkdir_p(logs);
    snprintf(last, sizeof last, "%s/last-run.log", logs);
    snprintf(prev, sizeof prev, "%s/previous-run.log", logs);
    remove(prev);
    rename(last, prev);
    if (!freopen(last, "w", stderr))
        return;
    setvbuf(stderr, NULL, _IONBF, 0);
    fflush(stdout);
    if (dup2(fileno(stderr), fileno(stdout)) < 0)
        fprintf(stderr, "[SWITCH] stdout not redirected (%s); only stderr"
                        " is in this log\n", strerror(errno));
    setvbuf(stdout, NULL, _IONBF, 0);
}

/* key=value lines (the file the engine's F9/F10 keys write to as well). */
static int cfg_get_int(const char *file, const char *key, int def)
{
    char line[512];
    size_t kl = strlen(key);
    int v = def;
    FILE *f = fopen(file, "r");
    if (!f) return def;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == '=') v = atoi(line + kl + 1);
    fclose(f);
    return v;
}

/* The same settings as the other launchers'. Full screen always (the
 * .nro owns the display). The AC97 one matters on Horizon: the APU's
 * fault-trapping build relies on signal machinery the port does not
 * have, and the no-trap build is the one the Switch platform layer is
 * written for. */
static const char *const k_env[][2] = {
    { "RECOMP_USB", "1" }, { "RECOMP_GL", "1" }, { "RECOMP_PB_EXEC", "1" },
    { "RECOMP_VBLANK", "1" }, { "RECOMP_AC97_READY_NOTRAP", "1" },
    { "RECOMP_TRACE_BUDGET", "0" }, { "RECOMP_GL_READBACK", "0" },
    { "RECOMP_FPS", "60" },
    { "RECOMP_ABI_RESTORE", "1" }, { "RECOMP_LOCK_IDLE", "1" },
    { "RECOMP_GL_CURTAIN", "skip" }, { "RECOMP_MUTE", "0" },
    { "RECOMP_WINDOW", "1" },
    { "RECOMP_FULLSCREEN", "1" },
};

/* Set when the binary is loaded, before the runtime's own constructors
 * run (priority 101 comes first): some of them read these, and on a
 * computer the launcher has set them before the engine starts.
 * Anything already set wins. */
__attribute__((constructor(101))) static void switch_env(void)
{
    size_t i;
    for (i = 0; i < sizeof k_env / sizeof k_env[0]; i++)
        setenv(k_env[i][0], k_env[i][1], 0);
}

/* The title-takeover check, and its explanation on the console
 * itself: if this message is all a player ever sees, it has to be
 * understandable there. */
extern int sw_mem_available(void);

static int launched_as_applet(void)
{
    /* The same test the guest memory makes (switch_mem.c): its five
     * process-memory syscalls hinted and a process handle passed, which
     * title takeover does and applet mode does not. */
    return !sw_mem_available();
}

static void say_launch_mode_problem(void)
{
    consoleInit(NULL);
    printf("JSRF Unleashed\n\n"
           "This was started in applet mode (through the Album),\n"
           "which does not give the game enough memory or the\n"
           "system calls its memory design needs.\n\n"
           "Start it through title takeover instead:\n"
           "hold R while launching any installed game, and pick\n"
           "JSRF Unleashed from the homebrew menu that opens.\n\n"
           "Press + to exit.\n");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        svcSleepThread(20000000ULL);
    }
    consoleExit(NULL);
}

/* First launch with only a disc image in game/: check it the way the
 * other launchers do, unpack it into the folder with the in-tree
 * unpacker, and say the result in the log. A wrong-build image is not
 * unpacked: the engine would refuse it anyway, and the folder stays
 * clean for the right one. */
static int ends_with_iso(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".iso");
}

static int unpack_progress(void *ctx, uint64_t done, uint64_t total)
{
    static uint64_t last_mb;
    uint64_t mb = done >> 20;
    (void)ctx;
    if (mb - last_mb >= 256 || done == total) {
        last_mb = mb;
        fprintf(stderr, "[SWITCH] unpacking: %llu of %llu MB\n",
                (unsigned long long)(done >> 20),
                (unsigned long long)(total >> 20));
    }
    return 0;
}

static void prepare_game_folder(const char *game)
{
    char xbe[1100];
    struct stat st;
    snprintf(xbe, sizeof xbe, "%s/default.xbe", game);
    if (stat(xbe, &st) == 0) return;   /* extracted folder: the norm */

    DIR *d = opendir(game);
    struct dirent *e;
    char iso[1100] = "";
    int found = 0;
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (ends_with_iso(e->d_name)) {
                snprintf(iso, sizeof iso, "%s/%s", game, e->d_name);
                found++;
            }
        }
        closedir(d);
    }
    if (found != 1) {
        if (found > 1)
            fprintf(stderr, "[SWITCH] %d disc images in %s; leave a "
                            "single image there, or unpack one "
                            "manually\n", found, game);
        else
            fprintf(stderr, "[SWITCH] no default.xbe and no disc image "
                            "in %s\n", game);
        return;
    }

    char why[640];
    int r = jsrf_xbe_check_iso(iso, why, sizeof why);
    if (r != JSRF_XBE_OK) {
        fprintf(stderr, "[SWITCH] %s\n", why);
        if (r == JSRF_XBE_OTHER)
            fprintf(stderr, "[SWITCH] %s\n", JSRF_XBE_OTHER_ADVICE);
        return;
    }
    fprintf(stderr, "[SWITCH] disc image OK; unpacking %s into %s\n",
            iso, game);
    char err[512] = "";
    if (xiso_extract(iso, game, unpack_progress, NULL, err, sizeof err) != 0) {
        fprintf(stderr, "[SWITCH] unpack failed: %s\n",
                err[0] ? err : "the image could not be unpacked");
        return;
    }
    r = jsrf_xbe_check_folder(game, why, sizeof why);
    if (r == JSRF_XBE_OK)
        fprintf(stderr, "[SWITCH] unpack complete; game folder ready\n");
    else
        fprintf(stderr, "[SWITCH] unpacked, but the folder check says: %s\n",
                why);
}

struct host_call { int (*host)(void); int rc; };

static void *host_thread(void *p)
{
    struct host_call *c = (struct host_call *)p;
    c->rc = c->host();
    return NULL;
}

int nv_switch_main(int argc, char **argv, int (*host)(void));
int nv_switch_main(int argc, char **argv, int (*host)(void))
{
    const char *base = SWITCH_BASE;
    char game[1024], save[1024], cfg[1024], tmp[1024], scale[8];
    struct host_call call;
    pthread_attr_t a;
    pthread_t t;

    if (launched_as_applet()) {
        say_launch_mode_problem();
        return 1;
    }

    /* argv[1], when hbmenu or a forwarder passes one, is the game
     * folder, as on the desktop. Otherwise the standard layout. */
    if (argc > 1 && argv[1] && argv[1][0])
        snprintf(game, sizeof game, "%s", argv[1]);
    else
        snprintf(game, sizeof game, "%s/game", base);
    snprintf(save, sizeof save, "%s/save", base);
    snprintf(cfg, sizeof cfg, "%s/settings", base);
    snprintf(tmp, sizeof tmp, "%s/tmp", base);
    mkdir_p(base);
    mkdir_p(game);
    mkdir_p(save);
    mkdir_p(tmp);

    open_log(base);
    { int s = cfg_get_int(cfg, "scale", 1);
      snprintf(scale, sizeof scale, "%d", s >= 1 && s <= 4 ? s : 1);
      setenv("RECOMP_GL_SCALE", scale, 0); }
    setenv("JSRF_GAME_DIR", game, 1);
    setenv("JSRF_SAVE_DIR", save, 1);
    setenv("JSRF_SETTINGS_FILE", cfg, 1);
    setenv("TMPDIR", tmp, 1);
    s_crash_fd = fileno(stderr);
    fprintf(stderr, "[SWITCH] JSRF Unleashed %s\n[SWITCH] game: %s\n"
                    "[SWITCH] data: %s\n", JSRF_VERSION, game, base);
    /* The base addr2line offsets in a crash report are relative to. */
    fprintf(stderr, "[SWITCH] module base %p\n", (void *)__start__);

    prepare_game_folder(game);
    if (chdir(game) != 0)
        fprintf(stderr, "[SWITCH] cannot enter %s: %s\n", game,
                strerror(errno));

    call.host = host;
    call.rc = 1;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 16u << 20);
    if (pthread_create(&t, &a, host_thread, &call) != 0) {
        fprintf(stderr, "[SWITCH] no thread for the engine\n");
        pthread_attr_destroy(&a);
        return 1;
    }
    pthread_attr_destroy(&a);
    pthread_join(t, NULL);
    fprintf(stderr, "[SWITCH] the engine returned %d\n", call.rc);
    return call.rc;
}
