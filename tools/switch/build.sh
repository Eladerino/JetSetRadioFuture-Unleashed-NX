#!/usr/bin/env bash
# build.sh -- build the JSRF Unleashed Switch fork end to end.
#
# Runs on the build machine inside WSL2 (Ubuntu). The disc dump and
# the generated C never leave that machine: the generated code is
# derived from SEGA's binary, and the fork's design keeps it local,
# exactly like upstream. Only logs are meant to be shared back.
#
# Usage:
#   tools/switch/build.sh /path/to/game.iso
#   tools/switch/build.sh /path/to/extracted-game-folder   (has default.xbe)
#
# The input image may be a bare XISO, a redump image, or a full-disc
# dump: tools.xiso probes the partition bases itself.
#
# Knobs (environment):
#   FORK_DIR   where the fork source lives (default: the checkout
#              this script is in)
#   FORK_URL   git URL to clone when FORK_DIR does not exist
#   WORK_DIR   scratch + output (default: ~/jsrf-switch-build)
#   JOBS       parallel compile jobs (default: 4; the generated code
#              is over a million lines, and each translation unit is
#              large -- 4 is a safe number for 32 GB)
#   SKIP_TOOLCHAIN=1   do not touch devkitPro (already installed)
#   SKIP_RECOMP=1      reuse port/recomp/gen from an earlier run (after a
#                      source change outside the generated code; regenerating
#                      touches every generated file and recompiles them all)
#   JSRF_ICON=path     a 256x256 baseline JPEG to embed as the .nro's
#                      icon in the homebrew menu. Kept outside the repo
#                      on purpose: the icon ships inside the .nro, so
#                      releases use devkitPro's default icon.
#   EXPORT_DIR  a folder the finished .nro is also copied to, for dragging
#              onto the SD card (default under WSL: Downloads\jsrf-unleashed-switch
#              in your Windows profile; "none" turns it off). Only the .nro
#              is replaced there; options.txt (written once, with every
#              option) and anything else kept there stays put.
#
# Output: $WORK_DIR/dist/switch/jsrf-unleashed/ ready to copy to the
# SD card, plus $WORK_DIR/dist/debug/ with the .elf and linker map
# (keep them: crash addresses resolve against them), and full logs in
# $WORK_DIR/logs/.
set -euo pipefail

FORK_DIR="${FORK_DIR:-}"
FORK_URL="${FORK_URL:-}"
WORK_DIR="${WORK_DIR:-$HOME/jsrf-switch-build}"
JOBS="${JOBS:-4}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

say()  { printf '\n== %s\n' "$*"; }
die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ $# -ge 1 ] || die "usage: $0 /path/to/game.iso | /path/to/game-folder"
INPUT="$1"
[ -e "$INPUT" ] || die "input not found: $INPUT"

mkdir -p "$WORK_DIR/logs"
LOG="$WORK_DIR/logs/build-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG") 2>&1
say "log: $LOG"

grep -qi microsoft /proc/version 2>/dev/null || \
    echo "note: this does not look like WSL2; the script assumes Ubuntu/apt."

# ---------------------------------------------------------------- toolchain
if [ "${SKIP_TOOLCHAIN:-0}" != "1" ] && [ ! -d /opt/devkitpro/devkitA64 ]; then
    say "installing devkitPro (Switch toolchain)"
    # The official apt route (apt.devkitpro.org/install-devkitpro-pacman),
    # with the two repairs this setup needed on a clean machine: the
    # pacman.conf that dpkg leaves as .dpkg-new, and an explicit GPG dir
    # for pacman-key.
    if [ ! -f /usr/share/keyrings/devkitpro-pub.gpg ] && \
       [ ! -f /usr/share/keyring/devkitpro-pub.gpg ]; then
        sudo mkdir -p /usr/share/keyrings
        wget -q -U "dkp apt" -O /tmp/devkitpro-pub.gpg \
            https://apt.devkitpro.org/devkitpro-pub.gpg
        sudo cp /tmp/devkitpro-pub.gpg /usr/share/keyrings/devkitpro-pub.gpg
    fi
    if [ ! -f /etc/apt/sources.list.d/devkitpro.list ]; then
        echo "deb [signed-by=/usr/share/keyrings/devkitpro-pub.gpg] https://apt.devkitpro.org stable main" \
            | sudo tee /etc/apt/sources.list.d/devkitpro.list >/dev/null
    fi
    sudo apt-get update
    sudo apt-get install -y devkitpro-pacman || true
    DKP=/opt/devkitpro/pacman
    [ -f "$DKP/etc/pacman.conf" ] || \
        sudo cp "$DKP/etc/pacman.conf.dpkg-new" "$DKP/etc/pacman.conf"
    GPGDIR="$DKP/etc/pacman.d/gnupg"
    sudo mkdir -p "$GPGDIR"
    sudo sed -i 's|^#GPGDir.*|GPGDir      = /opt/devkitpro/pacman/etc/pacman.d/gnupg/|' \
        "$DKP/etc/pacman.conf"
    sudo "$DKP/bin/pacman-key" --gpgdir "$GPGDIR" --init || true
    sudo "$DKP/bin/pacman-key" --gpgdir "$GPGDIR" --populate devkitpro
    sudo "$DKP/bin/pacman" -Sy --noconfirm
    sudo "$DKP/bin/pacman" -S --noconfirm switch-dev switch-sdl2 switch-mesa
fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
[ -x "$DEVKITA64/bin/aarch64-none-elf-gcc" ] || \
    die "devkitA64 not found under $DEVKITPRO (set SKIP_TOOLCHAIN=0 or install it)"
say "toolchain: $("$DEVKITA64/bin/aarch64-none-elf-gcc" --version | head -1)"

# ---------------------------------------------------------------- fork source
if [ -z "$FORK_DIR" ]; then
    FORK_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi
if [ ! -d "$FORK_DIR" ]; then
    [ -n "$FORK_URL" ] || die "no fork at $FORK_DIR and FORK_URL is unset. Unzip the fork beside this script as ./fork, or set FORK_URL."
    say "cloning fork from $FORK_URL"
    git clone "$FORK_URL" "$FORK_DIR"
fi
[ -f "$FORK_DIR/port/CMakeLists.txt" ] || die "$FORK_DIR does not look like the fork (no port/CMakeLists.txt)"
say "fork: $FORK_DIR"

# ---------------------------------------------------------------- game files
GAME_FILES="$WORK_DIR/game_files"
mkdir -p "$GAME_FILES"
cd "$FORK_DIR"
if [ ! -f .venv/bin/activate ]; then
    say "python venv + capstone"
    python3 -m venv .venv
fi
# shellcheck disable=SC1091
. .venv/bin/activate
pip install -q capstone

if [ -f "$INPUT" ]; then
    say "extracting default.xbe from image"
    python3 -m tools.xiso get "$INPUT" default.xbe -o "$GAME_FILES/" \
        || die "could not extract default.xbe -- not an Xbox disc image this tool reads?"
    say "extracting full game folder for the SD card"
    python3 -m tools.xiso unpack "$INPUT" -o "$WORK_DIR/game_extracted/" \
        || die "full unpack failed"
elif [ -d "$INPUT" ]; then
    [ -f "$INPUT/default.xbe" ] || die "$INPUT has no default.xbe"
    cp "$INPUT/default.xbe" "$GAME_FILES/default.xbe"
    rm -rf "$WORK_DIR/game_extracted"
    cp -r "$INPUT" "$WORK_DIR/game_extracted"
else
    die "input is neither a file nor a folder: $INPUT"
fi
[ -f "$GAME_FILES/default.xbe" ] || die "no default.xbe after extraction"

# ---------------------------------------------------------------- recompilation
if [ "${SKIP_RECOMP:-0}" = "1" ] && [ -d port/recomp/gen ]; then
say "reusing port/recomp/gen (SKIP_RECOMP=1)"
# The generated code compiles against its own copy of the runtime header,
# which the recompiler refreshes from templates/runtime on every run. A
# reused gen/ would keep an old copy, so refresh it here the same way: the
# generated .c files are unchanged, only how they are compiled.
if ! cmp -s templates/runtime/recomp_types.h port/recomp/gen/recomp_types.h; then
    cp templates/runtime/recomp_types.h port/recomp/gen/recomp_types.h
    say "refreshed port/recomp/gen/recomp_types.h from templates/runtime (all generated code recompiles)"
fi
else
say "recompiling (a few minutes; the count at the end is the verdict)"
# The pipeline reads game_files/ relative to the fork root in upstream's
# recipe; point it at the copy we just made.
mkdir -p game_files
cp "$GAME_FILES/default.xbe" game_files/default.xbe
python3 -m tools.xbe_parser game_files/default.xbe --json game_files/default_analysis.json
python3 -m tools.disasm       game_files/default.xbe
python3 -m tools.func_id      game_files/default.xbe
python3 -m tools.abi_analysis game_files/default.xbe
RECOMP_OUT="$(python3 -m tools.recomp game_files/default.xbe --all --split 1000 \
    --back-edge-yield --exclude-manual port/recomp_manual.c \
    --gen-dir port/recomp/gen 2>&1 | tee "$WORK_DIR/logs/recomp.log")"
echo "$RECOMP_OUT" | tail -3
if echo "$RECOMP_OUT" | grep -q "9333/9342 functions (0 failed)"; then
    say "recompilation matches the expected standalone-US build exactly"
else
    die "recompilation count differs from 9333/9342 (0 failed). This is the signature of a different disc build (PAL, JP, or the Sega GT 2002 combo). The fork runs the standalone US build only -- see the log at $WORK_DIR/logs/recomp.log"
fi
fi

# ---------------------------------------------------------------- engine build
say "building the Switch engine (-j$JOBS)"
cmake -S port -B "$WORK_DIR/build-switch" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    ${JSRF_ICON:+-DJSRF_ICON="$JSRF_ICON"}
cmake --build "$WORK_DIR/build-switch" -j"$JOBS"

NRO="$(find "$WORK_DIR/build-switch" -name '*.nro' | head -1)"
ELF="$(find "$WORK_DIR/build-switch" -name 'jsrf_recomp.elf' -type f | head -1)"
MAP="$(find "$WORK_DIR/build-switch" -name 'jsrf_recomp.map' | head -1)"
[ -n "$NRO" ] || die "build finished but no .nro was produced"

# ---------------------------------------------------------------- packaging
say "packaging"
DIST="$WORK_DIR/dist/switch/jsrf-unleashed"
rm -rf "$WORK_DIR/dist"
mkdir -p "$DIST/game" "$WORK_DIR/dist/debug"
cp "$NRO" "$DIST/jsrf-unleashed.nro"
cp -r "$WORK_DIR/game_extracted/." "$DIST/game/"
[ -n "$ELF" ] && cp "$ELF" "$WORK_DIR/dist/debug/jsrf_recomp.elf"
[ -n "$MAP" ] && cp "$MAP" "$WORK_DIR/dist/debug/jsrf_recomp.map"
cat > "$DIST/READ-ME-FIRST.txt" <<'EOF'
JSRF Unleashed for Switch

Copy this whole folder to the SD card as sdmc:/switch/jsrf-unleashed/
(so the .nro sits in that folder, beside game/).

Launch through title takeover: hold R while starting any installed
game, then pick JSRF Unleashed from the homebrew menu. Applet mode
(Album) will refuse, on screen, in plain words.

game/ holds the game files from your own disc (default.xbe and
Media/). If game/ ever contains only a single .iso instead, the
first launch checks it and unpacks it into the folder.

The log of every run is sdmc:/switch/jsrf-unleashed/Logs/last-run.log.
EOF
# ---------------------------------------------------------------- export
# dist/ is rebuilt from nothing every time, so files kept beside the .nro
# there (env, settings) vanished with each build. The export folder is
# never cleared: the new .nro replaces the old one and nothing else moves.
# The game folder is not copied (it is gigabytes and already on the card).
EXPORT_DIR="${EXPORT_DIR:-}"
if [ -z "$EXPORT_DIR" ] && command -v cmd.exe >/dev/null 2>&1 \
   && command -v wslpath >/dev/null 2>&1; then
    # (from C:, so cmd.exe does not complain about starting in a WSL path)
    WINPROFILE="$( (cd /mnt/c 2>/dev/null; cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null) | tr -d '\r' || true)"
    if [ -n "$WINPROFILE" ] && [ "$WINPROFILE" != "%USERPROFILE%" ]; then
        EXPORT_DIR="$(wslpath -u "$WINPROFILE")/Downloads/jsrf-unleashed-switch"
    fi
fi
if [ -n "$EXPORT_DIR" ] && [ "$EXPORT_DIR" != "none" ]; then
    say "copying the .nro to $EXPORT_DIR"
    mkdir -p "$EXPORT_DIR"
    cp "$DIST/jsrf-unleashed.nro" "$EXPORT_DIR/jsrf-unleashed.nro"
    cat > "$EXPORT_DIR/READ-ME.txt" <<'EOF'
Drag everything in this folder (except this file) onto the SD card's
switch/jsrf-unleashed/ folder, replacing what is there.

jsrf-unleashed.nro  is replaced by every build.
options.txt         every option, in Notepad. Made once, then yours: the build
                    never overwrites it. (It replaces env.txt and settings;
                    delete those from here and from the card.)

The game/ folder is not copied here; it is already on the card.
EOF
    # The options file, made once with every option written out at its
    # default: change a number in Notepad, save, copy it to the card. The
    # build never touches it again.
    if [ ! -f "$EXPORT_DIR/options.txt" ]; then
        printf '%s\r\n' \
            "# JSRF Unleashed for Switch -- options" \
            "# Change a value, save, and copy this file to the SD card beside" \
            "# jsrf-unleashed.nro. 1 means on, 0 means off. Lines starting with #" \
            "# are notes and are ignored. The log's [SWITCH] options: line shows" \
            "# what was read." \
            "" \
            "# ---- picture" \
            "" \
            "# Widescreen 16:9 in gameplay (the logos and title screen stay 4:3)." \
            "widescreen=1" \
            "" \
            "# Internal resolution: 1 = the Xbox's 640x480, 2 = twice that, up to 4." \
            "# Each step costs GPU time." \
            "scale=1" \
            "" \
            "# Texture sharpness at an angle (anisotropic filtering): 1 = off, 4, 8, 16." \
            "aniso=4" \
            "" \
            "# Frame rate cap: 60 as on the Xbox. 0 removes the cap (testing only)." \
            "fps=60" \
            "" \
            "# Wait for the display's refresh before showing each frame." \
            "# The same choice is in the in-game menu, under Display (Vsync)." \
            "RECOMP_VSYNC=0" \
            "" \
            "# ---- sound" \
            "" \
            "mute=0" \
            "" \
            "# ---- performance testing (leave at 0 for normal play)" \
            "" \
            "# Keep compiled shaders between runs, so stutters only happen once." \
            "shader_cache=1" \
            "" \
            "# Where each frame's time goes, written to the log every second." \
            "profile=0" \
            "" \
            "# Which functions each thread is busy in, every 10 seconds" \
            "# (turn into names with tools/switch/prof.sh)." \
            "sample=0" \
            "" \
            "# Write the run's log (Logs/last-run.log). 0 = no log file" \
            "# at all: logging writes nothing to the card, and a crash" \
            "# leaves no log behind." \
            "log=1" \
            "" \
            "# 1 = write every log line to the card at once (slower). Only for" \
            "# chasing a freeze; normally the log is written every 0.25 s." \
            "log_direct=0" \
            "" \
            "# Log time spent busy-waiting (spinning). A Developer page switch." \
            "JSRF_SPIN_LOG=0" \
            "" \
            "# 1 = let the game hand work to the GPU thread without waiting" \
            "# for it to finish the last batch. 0 = the old, slower hand-off." \
            "fast_gpu=1" \
            "" \
            "# In widescreen, draw full-screen effects (the boost smear and its" \
            "# like) across the whole frame instead of squeezing them into a" \
            "# 4:3 box. A Developer page switch." \
            "RECOMP_WIDE_FX_FULL=1" \
            "" \
            "# Key the vertex and texture caches with CRC32, which the CPU does" \
            "# in hardware. A Developer page switch." \
            "RECOMP_GL_CRC32HASH=1" \
            "" \
            "# Keep a snapshot of the push buffer's calls, so a crash report can" \
            "# say what the GPU was last told to do. A Developer page switch." \
            "RECOMP_PB_CALLSNAP=1" \
            "" \
            "# Four Developer switches are read by their presence alone, so" \
            "# they are NOT lines to set to 0 -- a RECOMP_PB_OVERLAP=0 line" \
            "# would turn it on. Add the line with =1 to turn one on; leave" \
            "# the line out for off. The in-game menu writes and removes" \
            "# these lines itself:" \
            "#   RECOMP_PB_OVERLAP=1       count push-buffer overlap, in the log" \
            "#   RECOMP_GL_MERGEPROBE=1    probe merging draws, log the findings" \
            "#   RECOMP_PB_SEMLOG=1        log GPU semaphore waits and releases" \
            "#   RECOMP_MENU_STICK_BOTH=1  both sticks in the game's own menus" \
            "" \
            "# 1 = the face buttons by their printed names (A confirms," \
            "# B goes back, X and Y likewise) instead of by Xbox position." \
            "# The same choice is in the in-game menu, under Controller." \
            "nintendo_layout=0" \
            "" \
            "# Any other engine variable can go here too as NAME=value" \
            "# (RECOMP_... or JSRF_...)." > "$EXPORT_DIR/options.txt"
    fi
    EXPORTED="$EXPORT_DIR"
fi

say "done"
echo "nro:  $DIST/jsrf-unleashed.nro ($(stat -c%s "$DIST/jsrf-unleashed.nro") bytes)"
echo "sha:  $(sha256sum "$DIST/jsrf-unleashed.nro" | cut -d' ' -f1)"
echo "copy: $WORK_DIR/dist/switch/jsrf-unleashed  ->  sdmc:/switch/jsrf-unleashed"
echo "logs: $WORK_DIR/logs/ (send these back if anything failed)"
if [ -n "${EXPORTED:-}" ]; then
    echo "windows: $(wslpath -w "$EXPORTED" 2>/dev/null || echo "$EXPORTED")"
fi
