#!/usr/bin/env bash
# prof.sh -- turn a run's [SWPROF] samples into function names.
#
# Usage:
#   tools/switch/prof.sh /path/to/last-run.log [jsrf_recomp.elf]
#
# The .elf defaults to the one the last build left in
# ~/jsrf-switch-build/dist/debug/. It must be the build that produced the
# log: the offsets mean nothing against any other.
#
# Prints, per thread and per ten-second report, the share of samples, the
# offset, and the function it falls in (with the source line when the
# function is engine code). The output names functions only, so it is
# fine to share; the .elf itself stays on this machine.
set -euo pipefail

LOG="${1:?usage: $0 last-run.log [jsrf_recomp.elf]}"
ELF="${2:-$HOME/jsrf-switch-build/dist/debug/jsrf_recomp.elf}"
ADDR2LINE="${ADDR2LINE:-${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-addr2line}"

[ -f "$LOG" ] || { echo "no log at $LOG" >&2; exit 1; }
[ -f "$ELF" ] || { echo "no .elf at $ELF (pass it as the second argument)" >&2; exit 1; }
command -v "$ADDR2LINE" >/dev/null 2>&1 || ADDR2LINE="$(command -v aarch64-none-elf-addr2line || true)"
[ -n "$ADDR2LINE" ] || { echo "aarch64-none-elf-addr2line not found" >&2; exit 1; }

# One addr2line call for every offset in the log, then the log is replayed
# with each offset replaced by its function.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
grep -a -o 'nro+0x[0-9a-f]*' "$LOG" | sed 's/nro+//' | sort -u > "$tmp/addrs" || true
[ -s "$tmp/addrs" ] || { echo "no [SWPROF] samples in $LOG (was JSRF_SAMPLE=1 in env?)" >&2; exit 1; }
"$ADDR2LINE" -f -C -e "$ELF" $(cat "$tmp/addrs") | paste - - > "$tmp/names"
paste "$tmp/addrs" "$tmp/names" > "$tmp/map"

LC_ALL=C awk -v map="$tmp/map" '
    BEGIN {
        while ((getline l < map) > 0) {
            split(l, f, "\t")
            src = f[3]; sub(/^.*\/(src|port)\//, "", src); sub(/ \(discriminator.*/, "", src)
            name[f[1]] = f[2] (src ~ /^\?/ ? "" : "  [" src "]")
        }
    }
    /\[SWPROF\] thread/ { sub(/^.*\[SWPROF\] /, ""); print "\n" $0; stacks = 0; next }
    /\[SWPROF\].*stack/ {
        match($0, /[0-9.]+%/); pct = substr($0, RSTART, RLENGTH)
        rest = $0; out = ""
        while (match(rest, /nro\+0x[0-9a-f]+|\?/)) {
            t = substr(rest, RSTART, RLENGTH); rest = substr(rest, RSTART + RLENGTH)
            if (t == "?") n = "?"
            else { n = name[substr(t, 5)]; sub(/  \[.*$/, "", n); if (n == "") n = t }
            out = out (out == "" ? "" : " < ") n
        }
        if (!stacks) { print "  -- call chains (where each wait is from) --"; stacks = 1 }
        printf "  %6s  %s\n", pct, out
        next
    }
    /\[SWPROF\].*nro\+0x/ {
        match($0, /nro\+0x[0-9a-f]+/); a = substr($0, RSTART + 4, RLENGTH - 4)
        match($0, /[0-9.]+%/); pct = substr($0, RSTART, RLENGTH)
        printf "  %6s  %-10s %s\n", pct, a, name[a]
        next
    }
    /\[SWPROF\].*outside/ { sub(/^.*\[SWPROF\] +/, "  "); print }
' "$LOG"
