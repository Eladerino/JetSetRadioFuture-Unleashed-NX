# Half-rate Phase 1 — implementation report

Date: Nov 5, 2026  
Status: **desk phase complete; not pushed; not hardware-proven**  
Base: main **5b0e0ec**  
Implementation commit: **ff88b76** in the LOQ fork `~/jsrf-nx-halfrate`  
Build directory: `~/jsrf-switch-build-halfrate`

## Result in one paragraph

Phase 1 replaces the shipped frame-cap throttle with a vblank-grid pacer. A divisible cap such as 30 against the port's 17 ms emulated-vblank grid now assigns presents to every second vblank, using an advancing slot that is never re-based on when a sleep happened to end. The wait uses slices of at most 1 ms plus a short spun tail, loading/pre-scene presents are exempt, native frames slower than the cap release immediately, and the hold is reported separately from present cost. With `RECOMP_VSYNC=1`, the implementation does not sleep against a second clock: it carries the divider in the swapchain interval instead. The Frame rate menu row now offers **60 / 30**, plus the pre-existing honest `CUSTOM (n)` display only when options.txt already contains another value; **UNLIMITED has been removed**.

This is deliberately a display-cadence fix at full game speed. It does not divide the guest's vblank clock, does not skip guest logic, and cannot make a heavy scene faster.

## Deliverables and build record

- Patch: `halfrate-phase1.patch`
  - Size: **24,561 bytes**
  - SHA-256: `5d9ac95a25017a9bdfb6dbc0ea45fb418b0919a22daafb790e6e0d0297d52780`
  - Apply-verified against a pristine 5b0e0ec tree; all four patched files were byte-identical to the implementation tree after application.
  - `git fsck --strict` was clean in the implementation fork.
- Files changed:
  - `src/kernel/kernel_bridge.c`
  - `src/kernel/nv2a_backend.h`
  - `src/nv2a_dk/nv2a_dk.c`
  - `src/nv2a_dk/nv2a_dk_menu.c`
- Build: one held foreground build using the tree's own pinned work directory.
  - Result: **BUILD OK**
  - NRO size: **44,882,117 bytes**
  - NRO SHA-256: `2acb6e61c91429de9a00b0e2d5f478e3a2e110ea99a8c9dde23b911b9911a1d5`
  - Version string: `0.4.1-switch-dirty`
  - The build's dist NRO and staged copy agreed.
  - The new `[perf]` text `frame cap held` is present in the NRO, proving the new accounting string—and therefore the linked implementation path—is in the artifact.
  - Build log on the LOQ: `~/jsrf-switch-build-halfrate/logs/build-20261105-210531.log`
- Before building, the previously staged NRO was snapshotted at:
  - `~/jsrf-halfrate-scratch/staged-nro-before-halfrate.nro`
  - SHA-256: `a9db2776cd0d96bacb3b1905b18735d0f734eee9a3e927c0063d90b571f542a9`

The matching byte size with the shipped v0.4.1 NRO is a coincidence and was **not** used as provenance evidence; the hash, version string, build output, and in-artifact string check are the evidence.

## The vblank-grid accessor

### Contract

`kernel_vblank_grid()` is implemented in `src/kernel/kernel_bridge.c` around line 2419 and declared in `src/kernel/nv2a_backend.h`:

```c
void kernel_vblank_grid(unsigned long *count,
                        uint64_t *next_ns,
                        uint64_t *period_ns);
```

Its exact contract is:

- `*count` is the number of vblank interrupts delivered so far. It is read with an atomic acquire load from `g_vblank_count`.
- `*next_ns` is the due time of the next tick, expressed in `CLOCK_MONOTONIC` nanoseconds.
- `*period_ns` is the grid period in `CLOCK_MONOTONIC` nanoseconds.
- `*period_ns == 0` means the vblank clock is disabled or has not ticked yet.
- The tick's former function-local state was moved to file scope:
  - `s_vblank_enabled`
  - `s_vblank_next_ms`
  - `s_vblank_period_ms`
- Those values are written only by the kernel timer thread inside `kernel_vblank_tick()`.
- The time conversion is exact rather than a second clock because this port's `GetTickCount64()` is `CLOCK_MONOTONIC` in whole milliseconds.
- The returned snapshot is an estimate by construction: the timer thread continues running while the accessor reads it. A consumer must therefore use **count as the release condition** and use `next_ns`/`period_ns` only to estimate how long to sleep.

That last point is implemented literally in `pace_hold()`: when a target vblank exists, the hold ends when a fresh grid snapshot reaches that count, not merely when the estimated time arrives.

## Implementation by prescription point

### 1. Advancing deadline — never reset to the post-sleep time

**Implemented, with the grid form used whenever the cap divides the vblank period.**

In `src/nv2a_dk/nv2a_dk.c`, the pacer state is in the rewritten frame-cap block beginning around line 3078:

- `s_pace_slot` records the vblank on which the last capped present released.
- For divisor `K`, the next present's slot is `s_pace_slot + K`.
- The slot advances by `K` per present. It is not recalculated from the time at which a wait ended.
- A frame arriving after its slot releases immediately. Its overshoot does not push the following frames' slots.
- Repeated late presents step forward without sleeping until they are back on a future slot.
- If the lag reaches at least `2*K` counts beyond the missed slot, the phase re-anchors to the current grid boundary (`count / K * K`), rather than trying to recover an obsolete phase indefinitely.
- The first present after a cap latch anchors `s_pace_slot` to the last grid boundary (`count / K * K`), then uses the next `K`th boundary as its slot.

For a hand-set cap that the grid cannot divide into `K >= 2`, or when no grid is available, the implementation uses GL's advancing monotonic-deadline form:

- `s_pace_deadline_ns` advances by the requested target period.
- A deadline more than one second stale re-anchors to now before advancing.
- If the presenter is genuinely more than four target periods behind the advanced deadline, the deadline restarts at now.
- Otherwise, a frame waits only until its own advancing deadline.

This covers, for example, a hand-set CUSTOM 45 without pretending that 22.22 ms is an integral number of 17 ms vblanks.

### 2. Phase-lock to the vblank grid

**Implemented.**

`pace_divisor()` in `src/nv2a_dk/nv2a_dk.c` around line 3150 calculates:

```text
K = round(target_period / grid_period)
```

with these rules:

- no cap -> `K = 0`, disengaged;
- no grid -> `K = 0`, causing the advancing-deadline fallback;
- cap at or above the grid rate -> `K = 0`, disengaged;
- otherwise `K` is clamped by construction to at least 1.

For the menu's 30:

- grid period = 17 ms;
- nominal target = 33.333... ms;
- `K = 2`;
- therefore a locked present cadence is **two vblanks, approximately 34 ms**.

That is not an implementation accident. The investigation established that the emulated 60 Hz grid is actually 17 ms because the period is rounded to whole milliseconds. A vblank-locked “30” cannot also be exactly 33.333 ms; Phase 1 chooses the single-grid cadence and accepts approximately 34 ms.

### 3. Sliced sleeps with a spun tail

**Implemented in `pace_hold()` around line 3200.**

The hold:

- sleeps in slices of at most **1 ms** using `svcSleepThread`;
- stops sleeping approximately **1.2 ms** before the deadline and spins for the final stretch;
- when a target vblank exists, repeatedly reads `kernel_vblank_grid()` and releases on the target count;
- has a guard deadline approximately one grid period after the target. A stalled or absent grid can therefore cost about one extra period, not hang the present path forever;
- returns the actual nanoseconds held so the caller can accumulate `s_pace_wait_ns` for the `[perf]` report.

### 4. Pre-scene / loading exemption

**Implemented.**

`present()` now calls:

```c
fpscap_throttle(show_loading);
```

at `src/nv2a_dk/nv2a_dk.c` around line 3438.

The gate follows GL's draw-count rule:

- The pacer tracks the number of draws in the frame just flipped.
- A non-loading frame with at least **100 draws** latches `s_pace_seen_scene`.
- Once latched, the scene is considered live for the rest of the session; a later low-geometry screen is not mistaken for initial loading.
- Loading presents (`show_loading != 0`) never pace and never set the latch.

This matters because this title's loader is frame-driven: throttling its presents can throttle the loading process itself through queue backpressure.

### 5. Keep the hold out of the `[perf]` “present” figure

**Implemented by separate accounting.**

The investigation allowed either physically moving the wait outside `dk_flip`'s timed region or accounting for it separately. Phase 1 uses separate accounting because the throttle must remain at its existing point inside `present()`:

- `t_pace` was added to the renderer statistics structure (`src/nv2a_dk/nv2a_dk.c` around line 216).
- `dk_flip()` samples `s_pace_wait_ns` before and after `present(0)`.
- It converts only that call's hold to system ticks, adds it to `t_pace`, and subtracts it from the elapsed time assigned to `t_flip`.
- The `[perf]` line now ends its present clause with:

```text
present %.1f, of which waiting for the GPU %.1f + syncs %.1f; frame cap held %.1f
```

Thus “present” remains the cost of presenting, while the frame cap's hold is visible as its own number and cannot silently masquerade as renderer work.

### 6. Rate-aware disengagement

**Implemented by deadline/slot construction.**

If a frame arrives after its assigned slot, `fpscap_throttle()` returns without holding it. Consequently:

- at the standard 99th Street patrol, whose native frame time is already about two vblanks, a 30 cap should add no wait;
- the patrol must not turn into a roughly 66/68 ms cadence;
- a late frame costs only itself and does not shift later slots by its overshoot.

At a cap of 60, `pace_divisor()` returns 0 because the target period (16.667 ms) is less than or equal to the 17 ms grid period. The pacer therefore never engages. The pre-scene bookkeeping still runs, but there is no hold and no divider, so the **shipped behaviour is preserved in effect**.

The no-op claim is desk-proven from the code path; the patrol part of the hardware proof below must still demonstrate it in the pooled median.

### 7. `RECOMP_VSYNC=1`: one master clock per configuration

**Implemented as a swap-interval divider; no grid sleeping in that configuration.**

The relevant functions are:

- `pace_interval_want()` around line 3161;
- its forward declaration around line 1398;
- `pace_interval()` around line 3177;
- application during `swapchain_make()` around line 1429.

Behaviour:

- Configured swap interval 0 means the shipped vsync-off configuration, so the pacer uses the vblank-grid sleep path.
- Configured swap interval nonzero means vsync is on. For a divisible cap with `K >= 2`, the wanted interval becomes `base_interval * K`.
- Before the first real scene, `pace_interval_want()` returns the configured base interval.
- When a cap is latched after a scene exists, and again when the first scene latches, `pace_interval()` applies a changed wanted interval to the live swapchain.
- `s_pace_interval` records the last value set by the divider (`-1` = untouched) so unchanged values are not repeatedly applied.
- While vsync is pacing through the swap interval, the grid branch returns without calling `pace_hold()`; the renderer does not sleep against a second master clock.

Known boundary: this mode, like the shipped vsync path itself, assumes an approximately 60 Hz panel. It does not attempt to discover or compensate for a non-60 Hz display mode.

### 8. Menu row: 60 / 30 only; UNLIMITED removed

**Implemented in `src/nv2a_dk/nv2a_dk_menu.c`.**

Changes include:

- `k_fpscap_choices` is now `{ "60", "30", s_fpscap_custom }`.
- `k_fpscap_values` is now `{ 60, 30 }`.
- `fpscap_custom()` treats any value other than 60 or 30—including a hand-set 0—as custom.
- `fpscap_get()` returns:
  - 0 for 60;
  - 1 for 30;
  - 2 for CUSTOM.
- `fpscap_set()` treats index 2 as reselection of the existing custom value.
- `row_nchoices()` now returns 3 when a custom value is live and 2 otherwise (previously 4/3 with UNLIMITED present).
- Existing reset behaviour that selects the first entry still selects 60.
- The renderer's handling of a hand-set 0 was not removed; it remains an options-file/developer state and is now honestly displayed as `CUSTOM (0)` if it occurs.

The row's help text now says, in substance, that 30 holds each picture for two frames, gives an even cadence at full game speed, smooths scenes that wobble between rates, does **not** make heavy scenes faster, and redraws the menu at the capped rate while active.

## Adaptations from the GL pacer—and why

The GL backend's `frame_pace` was used as the discipline reference, not copied mechanically. The adaptations are deliberate:

1. **No phase-easing port.**  
   GL eases its deadline phase toward a measured display phase. On DK in vsync-off mode, the grid slot *is* the phase: there is no second phase to ease toward. Adding GL's easing would reintroduce a second cadence. In vsync-on mode, the panel/swap interval is the master instead.

2. **The release condition is the vblank count when a grid exists.**  
   GL's pacer is time-based. Here, the guest's frame boundaries are discrete vblank events. Sleeping only until an estimated time could release between the grid points the design is trying to align to. `pace_hold()` therefore treats time as the sleep estimate and count as the release.

3. **The restart rule becomes grid re-anchoring in slot mode.**  
   GL restarts an advancing deadline after falling far enough behind. In slot mode, the equivalent is to abandon the obsolete phase and set the anchor to the current grid boundary once lag reaches the implementation's `2*K` threshold. In the undividable/no-grid fallback, GL's deadline-restart discipline is retained directly.

4. **Scene detection is GL's draw threshold plus the DK loading flag.**  
   GL uses the draw-count heuristic alone. DK already knows whether a present is the loader thread's loading present, so the implementation uses both: loading presents neither pace nor latch the scene; ordinary frames latch it at 100 or more draws.

5. **The spun tail is fixed at about 1.2 ms.**  
   This is GL's value. There is no DK-side display measurement from which to learn a per-system pace margin, so no adaptive margin was invented.

6. **Vsync-on is handled by changing the swap interval, not by sleeping.**  
   GL's code does not provide this DK configuration split. Sleeping while the swapchain also waits for refresh would create two competing masters. Phase 1 therefore uses the grid only when the configured interval is 0 and the swap interval when vsync is on.

## Explicitly not changed

The following were outside the commission and remain untouched:

- **Phase 2 / `NV097_FLIP_STALL` completion** — not attempted; the investigation flags a hang risk if a stall never clears.
- **`RECOMP_VBLANK_HZ`** — not changed and not used as the 30 implementation. Dividing it would divide game speed and couple input/audio to the divided clock.
- **Logic decimation** — not attempted; the recompiled guest advances one step per vblank and provides no mechanism for advancing two logic steps per displayed frame.
- **The guest frame loop, queue depth, executor, and vblank rate** — unchanged.
- **At-cap input polling** — unchanged; input remains sampled when the guest asks, at guest-frame cadence.

## What is desk-proven and what is not

### Desk-proven

- The implementation follows the prescribed hook and does not alter the vblank period.
- The patch applies cleanly to pristine 5b0e0ec and reproduces the implementation tree.
- The implementation commit is fsck-clean.
- A single held foreground build completed and produced the NRO figures above.
- The dist and staged NRO copies agree.
- The new perf-accounting string is present in the built NRO.
- By code inspection:
  - 30 maps to `K = 2` on the 17 ms grid;
  - late frames release without a hold;
  - 60 does not engage the divider;
  - loading presents cannot set the scene latch;
  - the frame-cap hold is subtracted from `t_flip` and accumulated separately;
  - vsync-on uses a multiplied swap interval rather than a grid sleep.

### Not proven

- No console was contacted for this desk phase.
- The build has not been booted.
- The current shipped 30's hardware signature has not yet been captured in this report's evidence set; that is proof step 0 and is being captured separately by the campaign coordinator.
- No claim is made yet about:
  - actual present intervals on hardware;
  - the sustained vblank:present ratio;
  - game-speed equivalence;
  - patrol performance;
  - FMV behaviour;
  - audio behaviour;
  - live 60 ↔ 30 switching;
  - the vsync-on configuration on a real panel.

## Hardware proof plan

### Step 0 — baseline signature of the current 30

Before judging Phase 1, capture the shipped/current build at `fps=30` with the same instruments and locations. This baseline is being captured separately by the campaign coordinator. It should include:

- a light scene where the guest can natively hold 60, such as the garage;
- the standard 99th Street patrol;
- `RECOMP_DK_HITCH_MS=17`, so every capped present interval is visible;
- the `[perf]` present and frame figures;
- vblank count versus renderer present/frame count over a timed window;
- a timed in-game event to distinguish judder from slow-motion degeneration.

Phase 1 is then compared against that signature, not against an assumed one.

### Step 1 — Phase 1, light-scene dwell

Run the Phase 1 build at cap 30 in the same light scene for at least five minutes.

PASS requires **all** of the following:

- Hitch-log present intervals cluster tightly at **33–34 ms**.
- **Zero** 16.7 ms hitch lines.
- **Zero** 50 ms hitch lines.
- Sustained **vblank:present ratio = 2.00:1** over the timed window.
- The menu opens and navigates, and switching Frame rate between 60 and 30 takes effect live.

### Step 2 — full game speed, not slow motion

During the cap-30 dwell, time a known in-game event—such as a song of known length or a graffiti timer—against wall clock.

PASS:

- The event matches wall clock within approximately **1%**.

A materially slower event means the implementation has divided the guest clock or otherwise leaked into the wrong layer, regardless of how even the present intervals look.

### Step 3 — patrol no-harm check

Run the standard 99th Street patrol under the campaign's normal stock-clock gate: the run counts only if its own samples are uniformly **1020/384/1331**.

PASS:

- The pooled patrol median remains within noise of the established **33.0 ms** baseline.
- The cap demonstrably behaves as a no-op at native approximately-two-vblank arrivals.
- In particular, the median must not double toward a 66/68 ms cadence.

### Step 4 — loading and pre-scene behaviour

Boot and load into a scene with cap 30 already selected.

PASS:

- Loading is not visibly or measurably throttled by the cap.
- Pacing begins only once a real scene is live.
- No stall or hang occurs if presents arrive before the vblank grid starts or while scene state is changing.

### Step 5 — FMV checkpoint

Play one Bink/FMV sequence at cap 30.

PASS:

- It plays at the correct speed.
- There is no repeated stutter step introduced by the divider.
- Audio remains synchronised to the sequence.

### Step 6 — audio check

Keep music playing through the light-scene and patrol dwells.

PASS:

- Music is continuous.
- No new keeper-stall log lines appear.
- No underrun pattern emerges that was absent from the step-0 baseline.

Any audio regression should be treated as evidence that the implementation affected a layer outside the present divider, because Phase 1 changes neither the vblank rate nor the audio ring.

### Step 7 — vsync-on configuration

Repeat a shorter light-scene dwell with `RECOMP_VSYNC=1` and cap 30.

PASS:

- Presents settle into the same approximately 34 ms cadence.
- The `[perf]` frame-cap hold remains effectively zero in this configuration, because pacing is being done by the swap interval rather than `pace_hold()`.
- No alternating long/short interval pattern appears.
- Switching back to 60 restores the configured base interval.

### Failure signatures

- **Bimodal or scattered 16.7 / 33 / 50 ms intervals:** the phase lock failed.
- **Uniform intervals but a slow in-game clock:** the wrong layer was divided; reject the build.
- **Patrol median doubles:** rate-aware disengagement failed.
- **Loading slows materially:** the pre-scene exemption failed.
- **Audio faults without a present-cadence fault:** inspect for an unintended coupling outside the present path before accepting any explanation.
- **Vsync-on cadence differs from vsync-off without a panel-rate explanation:** the configuration split has failed; do not treat the vsync-off result as covering it.

## Bottom line

Phase 1 is implemented exactly as a present-cadence fix: grid-locked, advancing, exempt before a scene, self-disengaging when the game is already slower than the cap, separately accounted, and split by vsync configuration so only one clock masters the cadence. The desk work and build are complete. The remaining question is entirely empirical and is covered by the proof plan above.
