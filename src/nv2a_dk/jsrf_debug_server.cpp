// The jsrf-unleashed commands for the shared debug server core (debug_server.h,
// vendored from SwitchWakerHD through spm-nx: the core knows sockets, lines and
// files; this file is the part that knows this port). spm-nx's version of this
// file is spm_debug_server.cpp, WakerHD's is runtime/src/platform/debug_switch.cpp;
// ours follows the same split.
//
// The gate is dkserver in options.txt, which the launcher turns into the
// JSRF_DKSERVER environment variable; jsrf_switch.c calls jsrf_debug_server_boot
// once, after the log is open, and with the switch off nothing here runs: no
// socket, no thread, no cost. It is boot-time only on purpose (the listener
// starts once), so the Developer page's DEBUG SERVER row says (RESTART), and
// the env command below refuses to flip JSRF_DKSERVER live: a live set could
// only lie about what a restart will do.
//
// What the server adds over the core's built-ins (ping, help, get, put, ls,
// rm, mkdir, log, logtext, press, hold, release, stick):
//   info:  the port, its version, the stage the game is in, the frame count;
//   env:   get/set of the engine's RECOMP_/JSRF_ environment variables, the
//          same words options.txt speaks, for steering a run live where a
//          reader re-reads them (most are read once, at start: the reply
//          says so);
//   flush: push the buffered log to Logs/last-run.log now (sw_log_flush);
//   mark:  a line in the log, then a flush, so a scripted run can bracket
//          the segment it cares about;
//   warp, shot, peek, poke, quit, reload answer err and say they are not in
//   this build yet: stage jumps live in the menu core's map-jump latch,
//   which the server cannot reach; the renderer has no frame capture; a
//   guest-memory read needs the crash handler's mapping-checked read
//   shared out of the launcher first; and quitting or reloading cleanly
//   wants the hbloader next-load path. Deploy today is the core's put:
//   upload jsrf-unleashed.nro over the old one, then relaunch by hand.
// A crash report is part of the run's log (the launcher writes [CRASH]
// lines into it), so it is fetched with the core's get: Logs/last-run.log,
// or Logs/previous-run.log after a restart.
//
// Input injection needs no command here: the core holds the injected state
// and the provider at the bottom of this file feeds it to the input layer's
// weak jsrf_debug_input hook in src/input/xinput_device.c, exactly as
// SwitchWaker's cos_debug.cpp feeds its shim.
//
// The log feed: the core streams what log_tap is handed. This port's log is
// a file (Logs/last-run.log) the launcher's own flusher writes four times a
// second, so a pump thread on the server's behalf tails that file from its
// start into log_tap, and the stream carries the whole session, boot lines
// included, without touching any of the game's log call sites. With the
// log off (no log=1 in options.txt) there is no file and the stream stays
// empty; the rest of the server works the same. The pump and the core's
// threads run at the lowest thread priority (0x3B): a scripted run must
// never take time from the game it is measuring.
#include "debug_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef __SWITCH__
#include <switch.h>
#endif

extern "C" {
#include "nv2a_dk_ui.h"  // dk_ui_frames(): the renderer's C contract
}

// The port's own entry points, declared here rather than included:
// jsrf_switch.c (the version, the log flush) and recomp_manual.c (the
// stage the game is in). All plain C, linked into the same .nro.
extern "C" {
const char *recomp_version(void);
int xbox_current_stage(void);
const char *xbox_stage_name(int n);
void sw_log_flush(void);
}

namespace JsrfDebugServer {
namespace {

std::atomic<bool> g_booted{false};
std::atomic<bool> g_pumpRun{false};
std::thread g_pump;
std::string g_root;  // the server's file root (Config.root), set at boot

void LogLine(const char *line) {
    std::fprintf(stderr, "%s\n", line);
    std::fflush(stderr);
}

// ---- info -----------------------------------------------------------------------------

debugsrv::Reply CmdInfo(const debugsrv::Args &) {
    const int stage = xbox_current_stage();
    const char *log = std::getenv("JSRF_LOG");
    char text[512];
    std::snprintf(text, sizeof text,
                  "game jsrf-unleashed\nversion %s\nstage %d %s\nframes %u\n"
                  "server port 6543\nroot %s\nlog %s\n",
                  recomp_version(), stage,
                  stage >= 0 ? xbox_stage_name(stage) : "-",
                  dk_ui_frames(), g_root.c_str(),
                  log && std::atoi(log) ? "on (Logs/last-run.log)" : "off (no log file: the log stream is empty)");
    return debugsrv::ok(text);
}

// ---- the engine's variables, as commands ------------------------------------------------
// The same variables options.txt sets through the launcher, read and written
// live. Most readers take their value once, at start; a set here steers only
// what re-reads, and persists nothing (options.txt is where a kept change
// lives: the Developer page writes it there).

bool EnvNameOk(const std::string &name) {
    size_t pre = 0;
    if (name.rfind("RECOMP_", 0) == 0) pre = 7;
    else if (name.rfind("JSRF_", 0) == 0) pre = 5;
    else return false;
    if (name.size() <= pre) return false;  // something past the prefix
    for (char ch : name)
        if (!(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9') && ch != '_') return false;
    return true;
}

debugsrv::Reply CmdEnv(const debugsrv::Args &a) {
    if (a.size() >= 2 && a[0] == "get") {
        if (!EnvNameOk(a[1])) return debugsrv::err("not an engine variable (RECOMP_... or JSRF_...)");
        const char *v = std::getenv(a[1].c_str());
        return debugsrv::ok(a[1] + "=" + (v ? v : "<unset>") + "\n");
    }
    if (a.size() == 3 && a[0] == "set") {
        if (!EnvNameOk(a[1])) return debugsrv::err("not an engine variable (RECOMP_... or JSRF_...)");
        // The one variable a running server must not flip: it is read once
        // at boot, so a live set could only lie about what a restart does.
        if (a[1] == "JSRF_DKSERVER")
            return debugsrv::err("JSRF_DKSERVER is boot-time only: set dkserver in options.txt (or the Developer page) and restart");
        setenv(a[1].c_str(), a[2].c_str(), 1);
        std::fprintf(stderr, "[server] env set %s=%s (debug server)\n", a[1].c_str(), a[2].c_str());
        return debugsrv::ok(a[1] + "=" + a[2] + "\n(set for this process only; most variables are read at start, "
                                               "and options.txt is unchanged)\n");
    }
    return debugsrv::err("usage: env get <NAME> | env set <NAME> <value>");
}

// ---- the log, driven from the server ------------------------------------------------------

debugsrv::Reply CmdFlush(const debugsrv::Args &) {
    sw_log_flush();
    return debugsrv::ok();
}

debugsrv::Reply CmdMark(const debugsrv::Args &a) {
    std::string text;
    for (size_t i = 0; i < a.size(); i++) {
        if (i) text += ' ';
        text += a[i];
    }
    std::fprintf(stderr, "[server] mark \"%s\"\n", text.c_str());
    sw_log_flush();
    return debugsrv::ok();
}

// ---- not in this build (the follow-ups, answered honestly) --------------------------------

debugsrv::Reply CmdWarp(const debugsrv::Args &) {
    return debugsrv::err(
        "warp is not in this build: stage jumps live in the menu core's map-jump latch, which "
        "the server cannot reach yet. Use the menu's map jump; a server warp is a follow-up, "
        "once that latch is exported from the menu core");
}

debugsrv::Reply CmdShot(const debugsrv::Args &) {
    return debugsrv::err(
        "shot is not in this build: the deko3d renderer has no frame capture or readback path "
        "yet. It is a follow-up");
}

debugsrv::Reply CmdPeek(const debugsrv::Args &) {
    return debugsrv::err(
        "peek is not in this build: the flat guest window faults on an unmapped read, so "
        "peeking first needs a mapping-checked read (the crash handler checks pages by hand "
        "in the launcher; that check is not shared yet). It is a follow-up");
}

debugsrv::Reply CmdPoke(const debugsrv::Args &) {
    return debugsrv::err("poke is not in this build (see peek); writing guest memory is a follow-up");
}

debugsrv::Reply CmdQuit(const debugsrv::Args &) {
    return debugsrv::err(
        "quit is not in this build: nothing yet ends the engine on the server's word; close "
        "the game from the HOME menu. It is a follow-up, with reload, through the hbloader "
        "next-load path");
}

debugsrv::Reply CmdReload(const debugsrv::Args &) {
    return debugsrv::err(
        "reload is not in this build: it needs the hbloader next-load path (put the new "
        "NRO, set the next load, shut down cleanly). put works today: upload "
        "jsrf-unleashed.nro over the old one, then relaunch by hand");
}

// ---- the log pump ---------------------------------------------------------------------------

void PumpMain() {
#ifdef __SWITCH__
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3B);
#endif
    const std::string path = g_root + "/Logs/last-run.log";
    std::string buf(64 << 10, '\0');
    long offset = 0;
    FILE *f = nullptr;
    while (g_pumpRun.load()) {
        if (!f) {
            f = std::fopen(path.c_str(), "r");
            offset = 0;
        }
        if (f) {
            if (std::fseek(f, 0, SEEK_END) == 0) {
                const long size = std::ftell(f);
                if (size < offset) offset = 0;  // the file was truncated: a new boot, start over
                if (size > offset && std::fseek(f, offset, SEEK_SET) == 0) {
                    long left = size - offset;
                    while (left > 0 && g_pumpRun.load()) {
                        const size_t want = size_t(std::min<long>(left, (long)buf.size()));
                        const size_t got = std::fread(&buf[0], 1, want, f);
                        if (!got) break;
                        debugsrv::log_tap(buf.data(), got);
                        offset += (long)got;
                        left -= (long)got;
                    }
                }
            }
        }
        // 100 ms between passes, in slices so stop() joins at once
        for (int i = 0; i < 10 && g_pumpRun.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (f) std::fclose(f);
}

void ThreadStart() {
#ifdef __SWITCH__
    // The lowest priority the port allows: the server's threads only run in
    // the game's gaps, even while a client is streaming the log at them.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3B);
#endif
}

}  // namespace
}  // namespace JsrfDebugServer

extern "C" void jsrf_debug_server_boot(const char *base) {
    using namespace JsrfDebugServer;
    const char *e = std::getenv("JSRF_DKSERVER");
    if (!e || !std::atoi(e) || g_booted.exchange(true)) return;
#ifdef __SWITCH__
    g_root = base;
#else
    const char *r = std::getenv("JSRF_DEBUG_ROOT");
    g_root = r && *r ? r : base;
#endif
    // The pump replays the log file from its start, so the stream covers
    // the boot too; keep_log covers the window between here and start().
    debugsrv::keep_log();
    debugsrv::add_command("info", "info                   game, version, stage, frame count", &CmdInfo);
    debugsrv::add_command("env", "env get <NAME>         an engine variable (RECOMP_... / JSRF_...)", &CmdEnv);
    debugsrv::add_command("flush", "flush                  push the buffered log to Logs/last-run.log now", &CmdFlush);
    debugsrv::add_command("mark", "mark <text>            a [server] mark line in the log, then a flush", &CmdMark);
    debugsrv::add_command("warp", "warp <stage>           not in this build (stage jumps live in the menu)", &CmdWarp);
    debugsrv::add_command("shot", "shot                   not in this build (no frame capture yet)", &CmdShot);
    debugsrv::add_command("peek", "peek <addr> [bytes]    not in this build", &CmdPeek);
    debugsrv::add_command("poke", "poke <addr> <word>     not in this build", &CmdPoke);
    debugsrv::add_command("quit", "quit                   not in this build", &CmdQuit);
    debugsrv::add_command("reload", "reload                 not in this build (put works; relaunch by hand)", &CmdReload);

    debugsrv::Config config;
    config.port = 6543;
    config.root = g_root;
    config.log = &LogLine;
    config.threadStart = &ThreadStart;
    if (!debugsrv::start(config)) {
        debugsrv::drop_log();
        g_booted = false;  // nothing is running; stop() stays a no-op through g_pumpRun
        return;
    }
    LogLine("[server] debug server listening on port 6543: dev builds only, local network, NO "
            "password (anyone who can reach the port can read and write files under the app "
            "folder, flip engine variables and press buttons)");
#ifdef __SWITCH__
    // A console under test is left alone for minutes at a time; spm-nx's
    // server does the same.
    appletSetAutoSleepDisabled(true);
#endif
    g_pumpRun = true;
    g_pump = std::thread(&PumpMain);
}

extern "C" void jsrf_debug_server_stop(void) {
    using namespace JsrfDebugServer;
    if (!g_booted.load()) return;
    g_pumpRun = false;
    if (g_pump.joinable()) g_pump.join();
    debugsrv::stop();
#ifdef __SWITCH__
    appletSetAutoSleepDisabled(false);
#endif
}

#ifdef __SWITCH__
// The input layer's poll (src/input/xinput_device.c) calls this through a
// weak declaration, inherited from SwitchWaker: the injected presses and
// sticks in the core's own words (libnx button bits; stick axes -32767..
// 32767, y up), and while the server runs the controller counts as
// connected even with none attached. Only the sticks the core overrides
// are written into the array; the rest is the caller's own state. This
// function must stay in this translation unit: jsrf_debug_server_boot
// above is what pulls the unit into the link (jsrf_switch.c references
// it), so the strong symbol is there for the input layer's weak call
// whenever the server code is.
extern "C" int jsrf_debug_input(uint64_t *buttons, int32_t sticks[4]) {
    if (!debugsrv::running()) return 0;
    const debugsrv::Injected in = debugsrv::injected();
    *buttons = in.buttons;
    for (int s = 0; s < 2; s++) {
        if (in.stick[s]) {
            sticks[2 * s] = int32_t(in.x[s] * 32767.0f);
            sticks[2 * s + 1] = int32_t(in.y[s] * 32767.0f);
        }
    }
    return 1;
}
#endif
