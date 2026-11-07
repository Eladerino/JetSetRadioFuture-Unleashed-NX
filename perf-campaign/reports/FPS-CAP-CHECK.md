# FPS cap check — why the overlay reads past 60 at fps=60 (Nov 6, 2026)

Desk source check, read-only. Trees: candidate lineage `~/jsrf-nx-rc042`
@ 53a83f2; shipped lineage `~/jsrf-nx` @ 5b0e0ec (v0.4.1).

**Brandon's firsthand verdict (settles the artifact question):** when the
counter goes past 60 in light gameplay, skating actually gets way faster —
the game speed itself increases. A pure counter artifact is dead. This
report therefore answers: what the number counts, what was supposed to
gate production at 60, where the gate went, and when.

## Verdict

**Mechanism:** at fps=60 the candidate has NO working frame-rate gate
anywhere in the flip/present path. The host pacer disables itself by
construction at any cap at or above the emulated vblank grid rate, and
with RECOMP_VSYNC=0 the swapchain imposes no wait either. Guest frame
completion is released by the backend queue at consume speed, not by any
clock — so in scenes light enough to consume faster than 60/s (menus,
garage, character select, light gameplay), the guest produces frames,
the host presents them, and the frame-locked game logic all run at
consume speed. Observed up to ~120.

**Origin: candidate-introduced.** The shipped v0.4.1 throttle is an
unconditional minimum-interval limiter that holds presents to the cap
in all modes. The half-rate phase-1 pacer (rc042 commit 53a83f2, via
halfrate-phase1.patch) replaced it with the vblank-grid divider, whose
divisor is 0 at cap 60 — the 60 mode's enforcement was traded away to
get the grid-locked 30 mode. The shipped 60 mode does not have the hole.

## 1. What the number counts (honest counter)

- `present()` (src/nv2a_dk/nv2a_dk.c:3712) submits its command list and
  calls `dkQueuePresentImage` (:3905), then increments `s_frames`
  exactly once (:3906). One present() = one swapchain present = +1.
- The overlay block (:3803, only when `s_show_fps > 0 && !show_loading`)
  recomputes over a window of at least half a second (:3824) as
  `fdelta / secs` where fdelta is the `s_frames` delta (:3827-3828):
  **host swapchain presents per second**. No path in present()
  increments twice for one displayed frame.
- Caveat, immaterial here: the loader thread also presents
  (`present(1)`, :3925) and those bump `s_frames` too, but the overlay
  is suppressed while `show_loading`, and the loader runs before
  gameplay. Sustained in-game readings are game presents.
- The number is real: presents genuinely exceed 60/s. Brandon's
  speed-up observation and the code agree.

## 2. What was supposed to gate fps=60, and the escape

Two gates exist on paper:

**(a) The emulated vblank clock.** The Switch launcher force-sets
RECOMP_VBLANK=1 (port/switch/jsrf_switch.c:505). A kernel tick thread
calls `kernel_vblank_tick()` (src/kernel/kernel_bridge.c:3130; tick at
:2429), which raises the guest's NV2A vblank interrupt; the period is
`(long long)(1000.0 / 60 + 0.5)` = **17 ms** (:2452) — a 58.8 Hz grid —
and `g_vblank_count` advances once per tick (:2479). The guest has an
ISR that claims this interrupt (seen in run logs). But nothing in the
flip path waits for the count to advance: the interrupt is delivered,
production is not conditioned on it.

**(b) The host frame cap.** options `fps=` is exported as RECOMP_FPS
(port/switch/jsrf_switch.c:541) and latched by the cap code in
nv2a_dk.c. In the candidate this is `fpscap_throttle(int show_loading)`
(:3234), called once per present (:3739). It divides the cap against
the grid via `pace_divisor` (:3158): the divisor is **0 whenever the
cap's period is at or below the grid period** — target 16.67 ms at
fps=60 vs grid 17 ms — `if (target_ns <= period_ns) return 0;`
(:3164). The throttle then returns without holding (:3280-3281,
comment: "at/above the grid's rate: never engages"). The design comment
states the assumption outright (:3124-3125):

> "A cap at or above the grid's own rate (60) never engages: the guest
> cannot complete frames faster than the clock pacing it."

**The assumption is false in this build.** Guest completion is released
by the backend queue, at consume speed:

- The executor side may queue at most RECOMP_QUEUE_FRAMES (2) unmade
  flips and waits only beyond that (src/kernel/nv2a_backend_queue.c
  :50-55; `q_flip` waits on W_FLIPS at :628-639; the wait predicate is
  `s_flips < s_flip_limit` at :246). Ring room is the only other wait.
- The consumer makes a flip by calling the backend's flip
  (queue :836-838) → `dk_flip()` (nv2a_dk.c:5558) → `present(0)`
  (:5566), and makes the guest's progress stores in order as it passes
  them (queue :840). Every release the guest can wait on therefore
  lands when the consumer arrives, not when a clock ticks.
- With RECOMP_VSYNC=0, `swap_interval()` returns 0 (nv2a_dk.c:1391) —
  "presents at once" — so the swapchain adds no panel wait in
  `dkQueuePresentImage` either.

Chain, in a light scene: guest renders fast → flips queue → consumer
presents immediately (pacers all disengaged) → flip slots and progress
stores release at once → guest renders the next frame. Frame-locked
game logic advances once per produced frame, so game speed scales with
the production rate. At the patrol's 33 ms there is nothing to escape
(render-bound); in light scenes there is nothing holding it back.

Why cap 30 still locks: target 33.3 ms > grid 17 ms gives divisor 2, so
presents release on every second vblank — the grid pacer works exactly
where it was aimed. The hole is specific to caps at/above the grid
rate, i.e. the shipped-default 60 mode.

Not derivable from this tree: why the observed ceiling sits near ~120
rather than higher (swapchain image pool depth / Horizon present
plumbing are below the port's source). The missing-gate mechanism is
proven; the exact ceiling is not, and would need present-timing
instrumentation to explain.

## 3. Origin: the shipped build does not have this hole

Shipped v0.4.1 (`~/jsrf-nx` @ 5b0e0ec), src/nv2a_dk/nv2a_dk.c:

- `frame_period_latch` (:3081-3085) sets `s_frame_period =
  armGetSystemTickFreq() / fps` (default 60 from RECOMP_FPS).
- `fpscap_throttle(void)` (:3089-3105) is an unconditional minimum
  interval between presents: `earliest = s_last_present +
  s_frame_period; if (now < earliest) svcSleepThread(...)`. No grid,
  no divisor, no vsync dependence, no scene gate. Called once per
  present (:3259).
- At fps=60 every present is held to ≥16.67 ms after the previous one
  began, so presents cannot exceed 60/s; queue backpressure (the same
  2-flip limit) then gates the guest at the same rate. Menus and light
  gameplay included.

halfrate-phase1.patch removes that throttle (`-static void
fpscap_throttle(void)`) and adds `pace_divisor` plus the grid pacer —
the replacement is where cap-60 enforcement went. **Regression
introduced by the half-rate phase-1 change in the candidate lineage;
not inherited from shipped.**

## 4. What a fix would have to touch (design only — not implemented)

- **src/nv2a_dk/nv2a_dk.c — `fpscap_throttle` / `pace_divisor`
  (:3158-3290 region).** The fix lives here, where the shipped gate
  lived. When the divisor comes out 0 *because the cap is at/above the
  grid rate* (as opposed to no cap / no grid), the throttle must still
  hold the present: either a minimum-interval hold at the cap period
  (the shipped discipline, in ticks) or a release keyed to the vblank
  count itself for the divisor-1 case (hold until the grid count
  passes the last present's slot — caps at the grid's own 58.8 Hz,
  which is the rate the guest's clock already runs on; note the
  inherent 60 vs 58.8 mismatch when choosing). The divisor ≥2 path
  (cap 30 and CUSTOM divisors) must stay exactly as is — it is
  hardware-proven.
- Nothing else needs to change for the gate itself: the queue
  (nv2a_backend_queue.c) already backpressures the guest once
  presents slow down, and `swap_interval()` / RECOMP_VSYNC semantics
  stay untouched.
- Verification a fix would need: [perf] `t_pace` > 0 in 60 mode in
  light scenes (it reads 0 there today); overlay never above 60 in
  menus/light gameplay with normal game speed by eye; 30 mode still
  locked at ~34 ms on the grid; tower-patrol medians unchanged (the
  plaza is render-bound below the cap either way).
