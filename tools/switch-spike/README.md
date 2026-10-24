# Spike v8: the engine's guest memory at full scale

Muse's spike (v1 to v7) proved on hardware that one heap block, turned into
code-memory windows, can be mapped any number of times with
`svcMapProcessMemory`. It did that at 1 MB with two extra views. The engine
needs 64 MB with 30 views (base, 28 mirrors, tiled aperture), about 960 map
calls, so this spike runs the engine's own `src/platform/switch_mem.c` through
exactly that shape, plus the operations the platform layer performs later
(protection changes, commits inside the reservation, unmapping and remapping a
view, a small view across a window seam).

## On the Switch

    cd tools/switch-spike && make

Copy `switch-spike.nro` to `sdmc:/switch/`, start it through title takeover
(hold R while launching a game, pick it in the homebrew menu). It writes
`sdmc:/switch/jsrf-spike8.log`. Send that file back. The last line says whether
every check passed; the timings in steps 2, 5 and 8 are the numbers worth
having either way.

No game files are involved.

## On Linux first

    tools/switch-spike/host/run.sh

Builds the same program against a stand-in for the libnx calls made of memfd
and mmap (`host/hz_mock.c`), runs it, then reruns it with a syscall failing at
chosen points to check the cleanup paths, under AddressSanitizer. That tests
the bookkeeping in `switch_mem.c`, not Horizon.
