# Switch testing notes — fast_gpu / make-space (25 Oct 2026, Sudo's handoff session)

Short version: the fast_gpu loading freeze is fixed and verified on hardware.
This note is the state of play so the next session does not have to re-derive
it from logs. (Full run-by-run analysis lives outside the repo, with Brandon.)

## What was wrong

All of it was the pushbuffer ring wrap, in two places:

1. **The make-space branch choice** (`port/recomp_manual.c`, `sub_001910E0`).
   D3D's spin loop (gen `sub_00191440`, x86 0x001914F0) is a plain unsigned
   compare with no wrap handling. Across a wrap (DMA_GET numerically ahead
   of DMA_PUT) it can never succeed: GET gets republished as the new smaller
   PUT and the waiting title submits nothing more. The flush-ack clamp from
   e9a50ef forced that spin for large distances; D3D's own answer takes it
   for small ones. Fix: across a wrap, always answer sleep-range; linear,
   clamp as before.
2. **The walk's jump search** (`src/kernel/xbox_memory_layout.c`, ack
   thread). On a wrap it searched for the ring's jump in a fixed 128 KB
   window, but the jump sits at the ring's end. With the previous walk
   ending at PUT 0x4D3A0 in a ring past 0x80000, the jump was never found,
   the head segment holding make-space's NOP(5) was never walked, and
   software method 5 was never even counted — so the sleeping branch had
   nothing to wake it. Fix: search bound is now 1 MB; the walk stops at the
   jump, so the bound is a cap, not a cost.

## Testing change (important)

- The wrap rule is **no longer gated on `JSRF_SPIN_LOG`**. A normal
  fast_gpu test needs only `fast_gpu=1` in options.txt. Take the
  `JSRF_SPIN_LOG=1` line **out** — it now only enables a throttled
  once-a-second [SPIN] poll, which costs performance and pollutes every
  measurement.
- `fast_gpu` is still **off by default**. Turning it on by default is a
  separate decision, after a longer soak.

## Verified on hardware (emuMMC, title takeover, CPU 1963 / GPU 1152 / mem 2133)

- Run 6 (walk fix in): first fast_gpu run past the save-load screen.
  Method 5 counted 200 / delivered 200. Presents reached frame 16,189
  (the freeze point had been 7,398 in three straight runs). It later froze
  once at character select, in the spin, cause unproven (see below).
- Run 7 (wrap rule at any distance): **no freezes in ~273 s** — title,
  save load, hideout, Dogenzaka, a boost finish. Method 5 counted 1,141 /
  delivered 1,141, dropped 0, across 1,717 make-space calls. Frame rates
  in line with the fast_gpu=0 baseline (heavy segment ~26 fps at ~580
  draws, lighter segments 32–53), and that is with the sampling profiler
  and logging on.

## Still open

- **Run 6's character-select freeze is unexplained.** The leading guess
  was a small-distance wrap; run 7 had zero small-distance wrap calls in
  1,717 (0 occurrences — the trap is rare if real) and no freeze. The
  sleep-range raise covers it if it ever occurs, and the [SPIN] poll
  (with JSRF_SPIN_LOG=1) would show the exact call. Do not treat it as
  understood.
- **One confirmation run of the ungated build is still owed**: same test
  with the JSRF_SPIN_LOG line removed from options.txt, proving the
  permanent path behaves like the gated one did.
- **Boost visuals in widescreen** (older parked item): Brandon reports a
  darker 4:3-shaped region inside the widescreen frame during a boost,
  with the boost distortion looking exaggerated. Suspect: the fullscreen
  effect quad renders/samples at native 640x480 while the main render is
  stretched. Cheap test not yet run: one boost with `widescreen=0`, plus
  a screenshot and `JSRF_BOOST_DUMP` frames.
- Perf polish is a separate thread now (title slowness at start,
  occasional skips in gameplay). Correctness comes first; the numbers
  above are the baseline to beat, measured with profiler overhead on.
