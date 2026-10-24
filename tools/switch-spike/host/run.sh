#!/bin/sh
# Run spike v8 on Linux, against a stand-in for the libnx memory syscalls
# built on memfd + mmap (hz_mock.c): same aliasing, same refusal to map over
# mapped memory, same per-window limit. It checks switch_mem.c's
# bookkeeping -- records, seams, gaps, cleanup after a failed syscall --
# before a Switch is involved. It says nothing about Horizon itself; the
# .nro does that.
#
#   tools/switch-spike/host/run.sh          all checks, then the failure cases
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/jsrf-spike8-host
mkdir -p "$out"
sed "s#sdmc:/switch/jsrf-spike8.log#$out/spike8.log#" "$here/../source/main.c" > "$out/spike_host.c"
cc -g -O1 -fsanitize=address,undefined -std=gnu11 -Wall -D__SWITCH__ \
   -I"$here/mock" -I"$here/../../../src/platform" \
   -o "$out/spike_host" "$out/spike_host.c" "$here/hz_mock.c" -lpthread
# The views' backing is kept for the life of the mapping, as in the engine.
export ASAN_OPTIONS=detect_leaks=0
status=0
run() {
    env "$@" "$out/spike_host" > /dev/null 2>&1 || true
    echo "--- ${*:-normal run}: $(grep RESULT "$out/spike8.log")"
}
run
grep -q "all checks passed" "$out/spike8.log" || status=1
# Each of these must fail the one check it targets and nothing else, and
# must not trip the sanitizers on the way out.
for inj in MOCK_APPLET=1 CODE_FAIL=20 VIEW_FAIL=45 VIEW_FAIL=962; do
    run "$inj"
    if grep -q "ERROR: AddressSanitizer\|runtime error" "$out/spike8.log"; then
        echo "    sanitizer report, see $out/spike8.log"; status=1
    fi
    [ "$(grep -c '  FAIL  ' "$out/spike8.log")" = 1 ] || { echo "    expected exactly one failed check"; status=1; }
done
exit $status
