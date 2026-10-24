#!/usr/bin/env bash
# build.sh -- build the JSRF Unleashed Switch fork end to end.
#
# Runs on Brandon's LOQ inside WSL2 (Ubuntu). His disc dump and the
# generated C never leave this machine: the generated code is derived
# from SEGA's binary, and the fork's design keeps it local, exactly
# like upstream. Only logs are meant to be shared back.
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
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK_DIR/build-switch" -j"$JOBS"

NRO="$(find "$WORK_DIR/build-switch" -name '*.nro' | head -1)"
ELF="$(find "$WORK_DIR/build-switch" -name 'jsrf_recomp' -type f | head -1)"
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
say "done"
echo "nro:  $DIST/jsrf-unleashed.nro ($(stat -c%s "$DIST/jsrf-unleashed.nro") bytes)"
echo "sha:  $(sha256sum "$DIST/jsrf-unleashed.nro" | cut -d' ' -f1)"
echo "copy: $WORK_DIR/dist/switch/jsrf-unleashed  ->  sdmc:/switch/jsrf-unleashed"
echo "logs: $WORK_DIR/logs/ (send these back if anything failed)"
