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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/iosupport.h>
#include <switch.h>
#include "xiso.h"
#include "../xbe_check.h"

/* Thread pointer in TPIDR_EL0 (JSRF_TP_EL0, set by CMakeLists.txt when the
 * compiler takes -mtp=tpidr_el0).
 *
 * libnx builds everything with -mtp=soft: every __thread access calls
 * __aarch64_read_tp, which loads the newlib thread pointer from slot 0x1F8
 * of the Horizon TLS block (tpidrro_el0). The guest's x86 registers are
 * __thread, and the profile put 8-11% of the game thread in that call.
 * Compiled with -mtp=tpidr_el0 the access is one mrs instead. TPIDR_EL0 is
 * the EL0-writable thread register, which the kernel saves and restores
 * per thread and nothing in libnx uses; this copies the same pointer into
 * it, so the offsets are unchanged. Every thread has to do that before it
 * runs this code: the main thread in userAppInit (before constructors),
 * every other one through the threadCreate wrapper (-Wl,--wrap) that all
 * of libnx's thread creation goes through, pthreads and SDL included. A
 * thread that missed it would fault near address 0, not run on silently. */
#ifdef JSRF_TP_EL0
static inline void tp_publish(void)
{
    uint64_t tp = *(uint64_t *)((uint8_t *)armGetTls() + 0x1F8);
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(tp));
}

void userAppInit(void)
{
    tp_publish();
}

Result __real_threadCreate(Thread *t, ThreadFunc entry, void *arg,
                           void *stack_mem, size_t stack_sz, int prio,
                           int cpuid);

typedef struct { ThreadFunc entry; void *arg; } TpStart;

static void tp_thread_entry(void *p)
{
    TpStart s = *(TpStart *)p;
    free(p);
    tp_publish();
    s.entry(s.arg);
}

Result __wrap_threadCreate(Thread *t, ThreadFunc entry, void *arg,
                           void *stack_mem, size_t stack_sz, int prio,
                           int cpuid)
{
    TpStart *s = malloc(sizeof *s);
    Result rc;
    if (!s)
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    s->entry = entry;
    s->arg = arg;
    rc = __real_threadCreate(t, tp_thread_entry, s, stack_mem, stack_sz,
                             prio, cpuid);
    if (R_FAILED(rc))
        free(s);
    return rc;
}
#endif

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
extern ptrdiff_t xbox_GetMemoryOffset(void);

/* The log file (open_log below). A crash report writes to it directly,
 * past the buffer and its locks; before the log is open, to stderr. */
static int s_log_fd = -1;
static void log_drain_final(void);

/* Where this .nro is loaded. __start__ is no use for this: the linker
 * script defines it as an absolute 0, which no relocation moves, so it
 * reads 0 at run time. The code segment comes first in the image, so the
 * region holding any function in it starts at the module base. */
static u64 module_base(void)
{
    static u64 base;
    MemoryInfo mi;
    u32 pi;
    if (!base && R_SUCCEEDED(svcQueryMemory(&mi, &pi, (u64)(uintptr_t)&module_base)))
        base = mi.addr;
    return base;
}

static void crash_puts(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    while (n) {
        ssize_t w = write(s_log_fd >= 0 ? s_log_fd : 2, s, n);
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
    u64 base = module_base();
    crash_hex(label, a);
    if (base && a >= base && a < base + 0x10000000ull)   /* a generous bound on .nro size */
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

    log_drain_final();
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

/* Logs/last-run.log, the one before it kept as previous-run.log.
 *
 * stdout and stderr (descriptors 1 and 2) are pointed at a device of
 * this file's own, so every write lands in memory and a thread moves it
 * to the card four times a second. Writing each line straight to the
 * card cost an IPC round trip to the file system per fprintf (stderr is
 * unbuffered, and the engine flushes after most lines besides): sampled
 * on the title screen, the game thread spent about a fifth of its time
 * in those calls and another fifth queued on the stream's lock behind
 * them, and the GPU thread an eighth. Both streams share one buffer, so
 * the lines stay in the order they were written.
 *
 * Two buffers: the flusher swaps them and writes the full one while the
 * engine fills the other. A buffer that fills before the flusher comes
 * round is written by the thread that filled it. A crash report drains
 * what is buffered before it writes, and then writes straight through
 * (log_direct below), as does ExitProcess. What a hard freeze can lose
 * is the last quarter second; log_direct=1 in options.txt writes every
 * line to the card as before, for chasing one.
 *
 * rename() on Horizon refuses an existing destination, hence the remove. */
#define LOG_BUF (256u << 10)
static char s_logbuf[2][LOG_BUF];
static size_t s_loglen;
static int s_logcur;
static volatile int s_log_direct;
static Mutex s_log_mu, s_log_io;   /* taken in that order */

static void log_out(const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(s_log_fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static ssize_t log_write_r(struct _reent *r, void *fd, const char *p, size_t n)
{
    (void)r; (void)fd;
    if (s_log_direct) {
        mutexLock(&s_log_io);
        log_out(p, n);
        mutexUnlock(&s_log_io);
        return (ssize_t)n;
    }
    mutexLock(&s_log_mu);
    if (s_loglen + n > LOG_BUF) {
        mutexLock(&s_log_io);
        log_out(s_logbuf[s_logcur], s_loglen);
        s_loglen = 0;
        if (n > LOG_BUF) { log_out(p, n); n = 0; }
        mutexUnlock(&s_log_io);
    }
    memcpy(s_logbuf[s_logcur] + s_loglen, p, n);
    s_loglen += n;
    mutexUnlock(&s_log_mu);
    return (ssize_t)n;
}

static const devoptab_t dotab_log = {
    .name    = "jsrflog",
    .write_r = log_write_r,
};

/* Move what is buffered to the card. */
static void log_drain(void)
{
    int b;
    size_t n;
    if (s_log_fd < 0) return;
    mutexLock(&s_log_mu);
    b = s_logcur;
    n = s_loglen;
    s_logcur ^= 1;
    s_loglen = 0;
    mutexLock(&s_log_io);
    mutexUnlock(&s_log_mu);
    log_out(s_logbuf[b], n);
    mutexUnlock(&s_log_io);
}

/* From the exception handler and on the way out: no waiting on a lock
 * the faulting thread may hold, and every later write goes straight to
 * the card. */
static void log_drain_final(void)
{
    int mu, io;
    if (s_log_fd < 0) return;
    s_log_direct = 1;
    mu = mutexTryLock(&s_log_mu);
    io = mutexTryLock(&s_log_io);
    log_out(s_logbuf[s_logcur], s_loglen);
    s_loglen = 0;
    if (io) mutexUnlock(&s_log_io);
    if (mu) mutexUnlock(&s_log_mu);
}

void sw_log_flush(void);
void sw_log_flush(void)
{
    fflush(NULL);
    log_drain_final();
}

static void log_flusher(void *arg)
{
    (void)arg;
    for (;;) {
        svcSleepThread(250000000ull);
        log_drain();
    }
}

static void open_log(const char *data)
{
    static Thread flusher;
    char logs[1024], last[1100], prev[1100];
    const char *d = getenv("JSRF_LOG_DIRECT");
    snprintf(logs, sizeof logs, "%s/Logs", data);
    mkdir_p(logs);
    snprintf(last, sizeof last, "%s/last-run.log", logs);
    snprintf(prev, sizeof prev, "%s/previous-run.log", logs);
    remove(prev);
    rename(last, prev);
    s_log_fd = open(last, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (s_log_fd < 0)
        return;
    s_log_direct = d && atoi(d) > 0;
    fflush(stdout);
    fflush(stderr);
    devoptab_list[STD_OUT] = &dotab_log;
    devoptab_list[STD_ERR] = &dotab_log;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (!s_log_direct
        && R_SUCCEEDED(threadCreate(&flusher, log_flusher, NULL, NULL, 0x4000, 0x2C, -2)))
        threadStart(&flusher);
    else
        s_log_direct = 1;
    atexit(log_drain_final);
    fprintf(stderr, "[SWITCH] log: %s\n", s_log_direct ? "written line by line (log_direct=1)"
                                                    : "buffered, written every 0.25 s");
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
    /* fast_gpu: on since it ran title, select and hideout cleanly
     * (2026-10-26); fast_gpu=0 in options.txt turns it off. */
    { "RECOMP_NV2A_FLUSH_ACK", "1" },
};

/* Set when the binary is loaded, before the runtime's own constructors
 * run (priority 101 comes first): some of them read these, and on a
 * computer the launcher has set them before the engine starts.
 * Anything already set wins.
 *
 * The console has no shell to set a variable in, so <base>/options.txt
 * stands in for one, in words a player can read: widescreen=1, scale=2,
 * '#' for a comment (the build writes one with every option listed). A
 * raw engine variable (RECOMP_..., JSRF_...) works there too, for the
 * rest. env.txt and env, the earlier names, are still read after it.
 * All of it is read first, so it wins over the defaults below and over
 * upstream's settings file. The SD card is mounted by the time
 * constructors run (libnx's __appInit). */

/* The friendly names. A switch the engine tests only for being set
 * (presence) is left unset by 0, not set to "0". */
static const struct { const char *name, *var; int presence; } k_opts[] = {
    { "widescreen",   "RECOMP_WIDESCREEN",   0 },
    { "scale",        "RECOMP_GL_SCALE",     0 },
    { "aniso",        "RECOMP_GL_ANISO",     0 },
    { "fps",          "RECOMP_FPS",          0 },
    { "mute",         "RECOMP_MUTE",         0 },
    { "shader_cache", "RECOMP_SHADER_CACHE", 0 },
    { "profile",      "RECOMP_GL_PROFILE",   1 },
    { "sample",       "JSRF_SAMPLE",         0 },
    { "log_direct",   "JSRF_LOG_DIRECT",     0 },
    { "fast_gpu",     "RECOMP_NV2A_FLUSH_ACK", 0 },
};

/* What the options files said, for the log (which is not open yet here). */
static char s_env_seen[768];
static int s_env_files;

static void opt_note(const char *fmt, const char *a, const char *b)
{
    size_t n = strlen(s_env_seen);
    snprintf(s_env_seen + n, sizeof s_env_seen - n, fmt, a, b);
}

__attribute__((constructor(101))) static void switch_env(void)
{
    size_t i;
    static const char *const names[] = {
        SWITCH_BASE "/options.txt", SWITCH_BASE "/env.txt", SWITCH_BASE "/env",
    };
    int n;
    for (n = 0; n < 3; n++) {
        FILE *f = fopen(names[n], "r");
        char line[512];
        if (!f) continue;
        s_env_files++;
        opt_note(" [%s]%s", strrchr(names[n], '/') + 1, "");
        while (fgets(line, sizeof line, f)) {
            char *k = line, *eq, *v, *end;
            if (!memcmp(k, "\xEF\xBB\xBF", 3)) k += 3;   /* a UTF-8 mark */
            while (*k == ' ' || *k == '\t') k++;
            eq = strchr(k, '=');
            if (*k == '#' || !eq || eq == k) continue;
            /* trim both sides of the '=' and the line's end */
            for (end = eq; end > k && (end[-1] == ' ' || end[-1] == '\t'); end--) ;
            *end = 0;
            v = eq + 1;
            while (*v == ' ' || *v == '\t') v++;
            end = v + strcspn(v, "\r\n#");
            while (end > v && (end[-1] == ' ' || end[-1] == '\t')) end--;
            *end = 0;
            for (i = 0; i < sizeof k_opts / sizeof k_opts[0]; i++)
                if (!strcasecmp(k, k_opts[i].name)) break;
            if (i < sizeof k_opts / sizeof k_opts[0]) {
                if (k_opts[i].presence && !strcmp(v, "0")) unsetenv(k_opts[i].var);
                else setenv(k_opts[i].var, v, 1);
                opt_note(" %s=%s", k, v);
            } else if (!strncmp(k, "RECOMP_", 7) || !strncmp(k, "JSRF_", 5)) {
                setenv(k, v, 1);
                opt_note(" %s=%s", k, v);
            } else {
                opt_note(" %s=? (not an option)%s", k, "");
            }
        }
        fclose(f);
    }
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

/* ---- cores ----------------------------------------------------------
 *
 * libnx starts every pthread on the process's default core and lets it
 * run nowhere else: the engine's own thread, the GPU thread and every
 * guest thread queued on core 0 while cores 1 and 2 sat idle. Each
 * thread the engine starts comes through here first: it may run on any
 * core the process owns (the kernel moves a waiting thread to an idle
 * core it is allowed on), and its preferred core is dealt round the
 * others, so the busy threads start apart. */
static u32 app_core_mask(void)
{
    static u32 mask;
    u64 m = 0;
    if (!mask) {
        if (R_SUCCEEDED(svcGetInfo(&m, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
            mask = (u32)m & 0x7;   /* core 3 is the system's */
        if (!mask) mask = 1;
    }
    return mask;
}

/* The engine's threads, for the sampling profiler below. */
#define MAX_PROF_THREADS 32
static struct { Handle h; unsigned id; char what[8]; } s_threads[MAX_PROF_THREADS];
static volatile int s_nthreads;

void sw_thread_spread(const char *what, unsigned id);
void sw_thread_spread(const char *what, unsigned id)
{
    static volatile int next;
    u32 mask = app_core_mask();
    int cores[3], n = 0, c, ideal;
    Result rc;
    for (c = 0; c < 3; c++) if (mask & (1u << c)) cores[n++] = c;
    /* core 0 last: the main thread and the system's work live there */
    ideal = cores[(__atomic_fetch_add(&next, 1, __ATOMIC_RELAXED) + 1) % n];
    {
        int k = __atomic_fetch_add(&s_nthreads, 1, __ATOMIC_RELAXED);
        if (k < MAX_PROF_THREADS) {
            s_threads[k].id = id;
            snprintf(s_threads[k].what, sizeof s_threads[k].what, "%s", what);
            __atomic_store_n(&s_threads[k].h, threadGetCurHandle(), __ATOMIC_RELEASE);
        }
    }
    rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, ideal, mask);
    fprintf(stderr, "[SWITCH] thread %s %u: cores 0x%X, prefers %d%s\n",
            what, id, mask, ideal, R_SUCCEEDED(rc) ? "" : " (refused)");
}

/* Every two seconds, how busy each core was, and now and then the clocks
 * (a profile's overclock applies to a title, and this says whether it
 * did). The kernel reports a core's idle time only to a thread running
 * on that core, so there is a small sampler pinned to each. */
static volatile u64 s_idle[3], s_tick[3];

static void core_sampler(void *arg)
{
    int core = (int)(intptr_t)arg;
    for (;;) {
        u64 idle = 0;
        svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, UINT64_MAX);
        s_idle[core] = idle;
        s_tick[core] = armGetSystemTick();
        svcSleepThread(2000000000ull);
    }
}

static void log_clocks(void)
{
    static int ok = -1;
    static const struct { PcvModuleId id; const char *name; } m[] = {
        { PcvModuleId_CpuBus, "cpu" }, { PcvModuleId_GPU, "gpu" }, { PcvModuleId_EMC, "mem" },
    };
    u32 hz[3] = { 0, 0, 0 };
    int i;
    if (ok < 0) ok = hosversionAtLeast(8, 0, 0) && R_SUCCEEDED(clkrstInitialize());
    if (!ok) { fprintf(stderr, "[SWPERF] clocks: not readable here\n"); return; }
    for (i = 0; i < 3; i++) {
        ClkrstSession s;
        if (R_SUCCEEDED(clkrstOpenSession(&s, m[i].id, 3))) {
            clkrstGetClockRate(&s, &hz[i]);
            clkrstCloseSession(&s);
        }
    }
    fprintf(stderr, "[SWPERF] clocks: cpu %u MHz, gpu %u MHz, mem %u MHz\n",
            hz[0] / 1000000, hz[1] / 1000000, hz[2] / 1000000);
}

static void perf_reporter(void *arg)
{
    u64 pi[3] = { 0 }, pt[3] = { 0 };
    u32 mask = app_core_mask();
    int round = 0, c;
    (void)arg;
    for (;; round++) {
        char line[160];
        int n = snprintf(line, sizeof line, "[SWPERF] cores busy:");
        svcSleepThread(2000000000ull);
        for (c = 0; c < 3; c++) {
            u64 i = s_idle[c], t = s_tick[c];
            if (!(mask & (1u << c))) continue;
            if (pt[c] && t > pt[c])
                n += snprintf(line + n, sizeof line - n, " %d:%3d%%", c,
                              (int)(100 - (i - pi[c]) * 100 / (t - pt[c])));
            pi[c] = i; pt[c] = t;
        }
        if (round) fprintf(stderr, "%s\n", line);
        if (round % 15 == 0) log_clocks();
    }
}

/* ---- sampling profiler (JSRF_SAMPLE=1 in <base>/env) ------------------
 *
 * Every two milliseconds each engine thread is paused for an instant and
 * its program counter read (svcSetThreadActivity, svcGetThreadContext3:
 * the kernel hands a process its own paused threads' registers). Every
 * ten seconds the busiest addresses per thread go to the log as offsets
 * into the .nro; tools/switch/prof.sh turns them into function names
 * with addr2line, on the machine that has the .elf. A thread asleep in
 * the kernel is counted at the svc it sleeps in, so waiting shows up as
 * waiting rather than vanishing. */
#define PROF_SLOTS 8192
typedef struct { u64 pc; u32 n; } ProfSlot;
static ProfSlot s_prof[MAX_PROF_THREADS][PROF_SLOTS];
static u32 s_prof_total[MAX_PROF_THREADS], s_prof_lost[MAX_PROF_THREADS];

/* The same samples again, keyed on the call chain: pc, lr and two saved
 * returns up the frame-pointer chain. A thread waiting in an svc shows
 * up above only as the svc; this says whose wait it is. */
#define PROF_DEPTH 6
#define STACK_SLOTS 2048
typedef struct { u64 a[PROF_DEPTH]; u32 n; } StackSlot;
static StackSlot s_stk[MAX_PROF_THREADS][STACK_SLOTS];

static void stack_add(int t, const u64 *a)
{
    u64 h = a[0] ^ (a[1] * 31) ^ (a[2] * 131) ^ (a[3] * 1031);
    u32 i = (u32)((h >> 2) * 2654435761u) & (STACK_SLOTS - 1), k;
    for (k = 0; k < 32; k++, i = (i + 1) & (STACK_SLOTS - 1)) {
        StackSlot *sl = &s_stk[t][i];
        if (!sl->n) { memcpy(sl->a, a, sizeof sl->a); sl->n = 1; return; }
        if (!memcmp(sl->a, a, sizeof sl->a)) { sl->n++; return; }
    }
}

static void stack_of(const ThreadContext *ctx, u64 *a)
{
    u64 fp = ctx->fp;
    int d;
    a[0] = ctx->pc.x;
    a[1] = ctx->lr;
    for (d = 2; d < PROF_DEPTH; d++) {
        const u64 *f;
        a[d] = 0;
        if (!crash_readable(fp)) continue;
        f = (const u64 *)(uintptr_t)fp;
        a[d] = f[1];
        if (f[0] <= fp) { fp = 0; continue; }
        fp = f[0];
    }
}

static void stack_dump(int t, u64 base, u32 total)
{
    enum { TOP = 12 };
    StackSlot top[TOP];
    int n = 0, i, j, d;
    for (i = 0; i < STACK_SLOTS; i++) {
        StackSlot sl = s_stk[t][i];
        if (!sl.n) continue;
        if (n < TOP) top[n++] = sl;
        else if (sl.n > top[TOP - 1].n) top[TOP - 1] = sl;
        else continue;
        for (j = n - 1; j > 0 && top[j].n > top[j - 1].n; j--) {
            StackSlot x = top[j]; top[j] = top[j - 1]; top[j - 1] = x;
        }
    }
    for (i = 0; i < n; i++) {
        char line[256];
        int k = snprintf(line, sizeof line, "[SWPROF]   %5.1f%%  stack",
                         100.0 * top[i].n / total);
        for (d = 0; d < PROF_DEPTH && top[i].a[d]; d++) {
            u64 a = top[i].a[d];
            if (a >= base && a < base + 0x10000000ull)
                k += snprintf(line + k, sizeof line - k, "%s nro+0x%llx", d ? " <" : "",
                              (unsigned long long)(a - base));
            else
                k += snprintf(line + k, sizeof line - k, "%s ?", d ? " <" : "");
        }
        fprintf(stderr, "%s\n", line);
    }
    memset(s_stk[t], 0, sizeof s_stk[t]);
}

static void prof_add(int t, u64 pc)
{
    u32 i = (u32)((pc >> 2) * 2654435761u) & (PROF_SLOTS - 1), k;
    for (k = 0; k < 32; k++, i = (i + 1) & (PROF_SLOTS - 1)) {
        if (s_prof[t][i].pc == pc) { s_prof[t][i].n++; return; }
        if (!s_prof[t][i].pc) { s_prof[t][i].pc = pc; s_prof[t][i].n = 1; return; }
    }
    s_prof_lost[t]++;
}

static void prof_dump(u64 base)
{
    int t, nt = s_nthreads < MAX_PROF_THREADS ? s_nthreads : MAX_PROF_THREADS;
    for (t = 0; t < nt; t++) {
        enum { TOP = 24 };
        ProfSlot top[TOP];
        int nt2 = 0, i, j;
        u32 total = s_prof_total[t];
        if (total < 50) continue;
        for (i = 0; i < PROF_SLOTS; i++) {
            ProfSlot sl = s_prof[t][i];
            if (!sl.n) continue;
            if (nt2 < TOP) top[nt2++] = sl;
            else if (sl.n > top[TOP - 1].n) top[TOP - 1] = sl;
            else continue;
            for (j = nt2 - 1; j > 0 && top[j].n > top[j - 1].n; j--) {
                ProfSlot x = top[j]; top[j] = top[j - 1]; top[j - 1] = x;
            }
        }
        fprintf(stderr, "[SWPROF] thread %s %u: %u samples\n",
                s_threads[t].what, s_threads[t].id, total);
        for (i = 0; i < nt2; i++) {
            u64 pc = top[i].pc;
            if (pc >= base && pc < base + 0x10000000ull)
                fprintf(stderr, "[SWPROF]   %5.1f%%  nro+0x%llx\n",
                        100.0 * top[i].n / total, (unsigned long long)(pc - base));
            else
                fprintf(stderr, "[SWPROF]   %5.1f%%  0x%llx (outside the .nro)\n",
                        100.0 * top[i].n / total, (unsigned long long)pc);
        }
        stack_dump(t, base, total);
        memset(s_prof[t], 0, sizeof s_prof[t]);
        s_prof_total[t] = s_prof_lost[t] = 0;
    }
}

static void prof_sampler(void *arg)
{
    u64 base = module_base(), last = armGetSystemTick();
    (void)arg;
    for (;;) {
        int t, nt = s_nthreads < MAX_PROF_THREADS ? s_nthreads : MAX_PROF_THREADS;
        svcSleepThread(2000000ull);
        for (t = 0; t < nt; t++) {
            Handle h = __atomic_load_n(&s_threads[t].h, __ATOMIC_ACQUIRE);
            ThreadContext ctx;
            if (!h || R_FAILED(svcSetThreadActivity(h, ThreadActivity_Paused))) continue;
            if (R_SUCCEEDED(svcGetThreadContext3(&ctx, h))) {
                u64 chain[PROF_DEPTH];
                prof_add(t, ctx.pc.x);
                stack_of(&ctx, chain);
                stack_add(t, chain);
                s_prof_total[t]++;
            }
            svcSetThreadActivity(h, ThreadActivity_Runnable);
        }
        if (armTicksToNs(armGetSystemTick() - last) >= 10000000000ull) {
            prof_dump(base);
            last = armGetSystemTick();
        }
    }
}

static void start_perf_log(void)
{
    static Thread samplers[3], reporter;
    u32 mask = app_core_mask();
    int c;
    for (c = 0; c < 3; c++)
        if ((mask & (1u << c))
            && R_SUCCEEDED(threadCreate(&samplers[c], core_sampler, (void *)(intptr_t)c,
                                        NULL, 0x2000, 0x20, c)))
            threadStart(&samplers[c]);
    if (R_SUCCEEDED(threadCreate(&reporter, perf_reporter, NULL, NULL, 0x8000, 0x2C, -2)))
        threadStart(&reporter);
    if (getenv("JSRF_SAMPLE") && atoi(getenv("JSRF_SAMPLE")) > 0) {
        static Thread sampler;
        /* above the engine's threads, so a sample lands when it is due */
        if (R_SUCCEEDED(threadCreate(&sampler, prof_sampler, NULL, NULL, 0x8000, 0x24, -2))) {
            svcSetThreadCoreMask(sampler.handle, 0, app_core_mask());
            threadStart(&sampler);
            fprintf(stderr, "[SWPROF] sampling every 2 ms, reported every 10 s\n");
        }
    }
}

static void *host_thread(void *p)
{
    struct host_call *c = (struct host_call *)p;
    sw_thread_spread("engine", 0);
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
    fprintf(stderr, "[SWITCH] JSRF Unleashed %s\n[SWITCH] game: %s\n"
                    "[SWITCH] data: %s\n", JSRF_VERSION, game, base);
    /* The base addr2line offsets in a crash report are relative to. */
    fprintf(stderr, "[SWITCH] module base %p\n", (void *)module_base());
    fprintf(stderr, "[SWITCH] options:%s\n", s_env_files ? s_env_seen
            : " none (no options.txt beside the .nro)");
#ifdef JSRF_TP_EL0
    fprintf(stderr, "[SWITCH] thread pointer: TPIDR_EL0\n");
#else
    fprintf(stderr, "[SWITCH] thread pointer: soft (libnx call)\n");
#endif

    prepare_game_folder(game);
    if (chdir(game) != 0)
        fprintf(stderr, "[SWITCH] cannot enter %s: %s\n", game,
                strerror(errno));

    /* The main thread only waits from here on, but it may wait anywhere. */
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, app_core_mask());
    start_perf_log();

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
