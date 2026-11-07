# Half-rate investigation — where 30 fps goes wrong in JSRF NX

Desk/source-level investigation, Nov 5, 2026. **Zero console contact.**
Commissioned by Brandon: the DISPLAY page's FRAME CAP 30 idea is gated on
real half-rate pacing support, and he wants to know *where* it goes wrong
before anyone builds the fix.

**Tree read:** `~/jsrf-nx` on the LOQ, at main **5b0e0ec** exactly (the
v0.4.1-switch base). The buildi tree's extra commits (5a520b1, build J)
are menu/post-processing work and touch none of the files cited here.
All line numbers are that tree. Claims are labelled **[source]** (read in
the code), **[campaign]** (hardware-measured earlier, cited), or
**[desk]** (mechanism reasoning from the source — plausible, unproven on
hardware until the proof in §4 runs).

## Verdict up front

1. **A 30 fps option already exists and is already the naive kind.**
   The Display page's Frame rate row offers 60 / 30 / UNLIMITED
   (`src/nv2a_dk/nv2a_dk_menu.c:506-507`), and 30 drives a sleep in the
   present path (`fpscap_throttle`, `src/nv2a_dk/nv2a_dk.c:3089-3106`).
   Nothing about it touches the guest's clock.
2. **The guest's clock is the emulated vblank, and it is not
   divisible.** The kernel raises a display interrupt at a nominal
   60 Hz and the game's frame loop waits on it. Anything that divides
   *that* (the tree even ships the lever, `RECOMP_VBLANK_HZ`) divides
   game speed itself — slow motion, not a frame cap.
3. **The naive cap fails in the present path in three specific ways**
   (§3): it throttles the renderer queue instead of pacing anything,
   its deadline resets every frame so sleep overshoot compounds (the
   GL backend's pacing code documents this exact bug and its fix —
   never ported to the Switch renderer), and its wall-clock phase is
   unrelated to both the vblank phase and the panel refresh, so the
   two cadences beat against each other. That beating is the judder.
4. **A correct half-rate mode is a display-cadence mode, not a
   game-speed mode** (§4): an even, vblank-phase-locked 33.3 ms
   present cadence at full game speed, hooked where the throttle
   already sits (or, in the purer phase-2 design, in the flip stall
   itself — the executor's own comment asks for exactly that).
   True logic decimation (the guest advancing two steps per shown
   frame) is **not expressible** in this architecture at all.

## 1. How pacing works today

There are three clocks in the port, and keeping them separate is the
whole story.

### 1a. The guest's frame clock: the emulated vblank

- The Switch launcher hardcodes `RECOMP_VBLANK=1` and `RECOMP_FPS=60`
  in its environment defaults (`port/switch/jsrf_switch.c:501,503`);
  options.txt's `fps=` line maps to `RECOMP_FPS`
  (`port/switch/jsrf_switch.c:537`).
- A kernel timer thread loops on a 1 ms sleep
  (`src/kernel/kernel_bridge.c:3001`) and calls `kernel_vblank_tick()`
  — commented "the GPU's frame clock" — at `kernel_bridge.c:3101`.
- The tick (`kernel_bridge.c:2398-2452`): period from
  `RECOMP_VBLANK_HZ`, default 60.0, converted to whole milliseconds —
  `period = (long long)(1000.0 / hz + 0.5)`, i.e. **17 ms at "60 Hz"**
  (`kernel_bridge.c:2418-2427`), with catch-up logic so a late tick
  doesn't push the next one out. It sets PCRTC_INTR_0's VBLANK bit
  plus a pending marker and PMC_INTR_0's PCRTC bit, bumps
  `g_vblank_count`, and raises interrupt vector 3
  (`kernel_bridge.c:2444-2452`). `g_vblank_count` exists, per its
  comment, to be "read by the renderer's pacing report, so 'the
  guest's frame clock' is a number and not a belief"
  (`kernel_bridge.c:2393-2396`).
- The title's ISR queues a DPC; the DPC signals the device's
  vertical-blank event; `D3DDevice_BlockUntilVerticalBlank` waits on
  that event with no timeout (`src/kernel/xbox_memory_layout.c:800-816`
  — the long comment there is the post-mortem of the music fault this
  chain once caused). JSRF's frame loop is paced by that wait, which
  is why campaign frame times quantise to vblank multiples
  **[campaign: FLIP-RESULTS F6 — no frame population exists between
  17 and 29 ms; frames land on 16.7 ms steps]**.
- The acknowledgement half of the interrupt is completed by
  `vblank_intr_tick` (`xbox_memory_layout.c:848`), run by three
  parties: the ack thread, a **keeper thread** that also moves
  KeTickCount whenever the ack thread has been away >2 ms and ends
  vblank waits roughly every millisecond
  (`xbox_memory_layout.c:901-905, 908-960`), and
  `xbox_nv2a_vblank_service()` called from pacing code.
- The guest's *other* clock is wall time: KeTickCount is written
  straight from the host's `GetTickCount()`
  (`src/kernel/kernel_bridge.c:225`). And D3D's display-field pin —
  which JSRF's own vblank ISR reads — follows the *parity of
  g_vblank_count* (`src/kernel/kernel_hal.c:806-820`). So different
  guest subsystems genuinely run on different clocks: frame-stepped
  logic on the vblank, tick readers on the wall. This matters in §2.

### 1b. The flip → queue → present path (where frames become pictures)

- The guest ends a frame with NV2A flip methods; the pushbuffer
  executor completes the flip **the moment it is asked** and calls
  the backend's flip. The executor's own comment is the thesis of
  this whole report: *"the flip completes the moment it is asked
  for, because there is no scanout to be in the middle of… Pacing
  wants the vblank clock in the kernel, not a sleep in here"*
  (`src/kernel/nv2a_pb_exec_dk.c:132-145`; dispatch at 2355-2356).
- Executor and renderer are decoupled by a record queue with a
  **flip limit**: at most `RECOMP_QUEUE_FRAMES` (default **2**) flips
  may be outstanding; the executor *waits* when the limit is hit
  (`src/kernel/nv2a_backend_queue.c:50-54, 628-639, 954`). This
  backpressure valve is the only coupling between how fast the
  renderer presents and how fast the guest may produce frames.
- On the queue thread, a flip record dispatches to the renderer's
  `dk_flip` (`nv2a_backend_queue.c:~836`; registered at
  `src/nv2a_dk/nv2a_dk.c:5125`), which calls `present()`
  (`nv2a_dk.c:5069-5080`). `present()` blits the last drawn surface
  into a swapchain image and calls `dkQueuePresentImage`
  (`nv2a_dk.c:3232-3417`). The menu overlay is drawn *inside*
  present, over the finished picture (`nv2a_dk.c:3410-3415`) — so
  anything that throttles present throttles the menu too.
- The swapchain's vsync is separate again: `RECOMP_VSYNC` picks the
  swap interval at chain creation, 1 = wait for refresh, 0 = present
  at once, unset = 1 (`nv2a_dk.c:1383-1392`). The shipped/campaign
  configuration runs `RECOMP_VSYNC=0`, so **the panel is not pacing
  anything**; all visible cadence comes from when presents happen.

### 1c. The existing rate machinery (full inventory)

| Lever | Where | What it actually does |
|---|---|---|
| `RECOMP_FPS` present throttle | `nv2a_dk.c:3073-3106`, menu row `nv2a_dk_menu.c:497-541` | Minimum wall-clock interval between presents, enforced by sleeping inside `present()`. Default 60. **This is the "30" that judders.** |
| GL `frame_pace` | `src/nv2a_gl/nv2a_gl.c:11717+` | The desktop backend's mature version of the same idea — see §3.2. **Not compiled into the Switch build** (`RECOMP_GL=0`, `jsrf_switch.c:497-500`). |
| `RECOMP_VSYNC` | `nv2a_dk.c:1383-1392` | Swapchain present interval 0/1. |
| `RECOMP_VBLANK` / `RECOMP_VBLANK_HZ` | `kernel_bridge.c:2398-2431` | The guest's frame-clock rate itself. A divider in disguise — §2. |
| `RECOMP_QUEUE_FRAMES` | `nv2a_backend_queue.c:954` | Flip backpressure depth, default 2. |
| `RECOMP_PACE_VBLANK` | `nv2a_gl.c:11837-11874` | GL only: pace sleeps are sliced so vblank acks get serviced during them. (On Switch the keeper thread covers this need instead.) |
| APU ring target | `src/apu/apu_core.c:522-545` | Audio is produced against the *device's* clock (Switch pulls 2048 frames ≈ 43 ms), ring held at `RECOMP_APU_RING_MS` (default 40 ms) — deliberately decoupled from frame rate. |
| Dynamic res scale | renderer | GPU-load lever, not a rate lever. |

There is **no frame-skip / decimation machinery anywhere** in the
port's own code (tree-wide search: the only "decimate" hits are a
Mesa header's GL extension names). **[source]**

### 1d. The other cadences a rate change would touch

- **Input** is sampled when the guest asks: `pad_read()`
  (`port/recomp_manual.c:3262`) is called from the guest's
  XInputGetState thunk (`recomp_manual.c:3418`) — i.e. once per
  guest logic frame in JSRF's loop. Input cadence *is* guest-frame
  cadence. (The input recorder keys its records to this poll
  sequence — halving polls halves record density.)
- **Audio output** runs on the device clock (§1c), but the guest's
  *refill* threads are frame-coupled: CRI's ADX server was one of
  the threads parked in `BlockUntilVerticalBlank`, refilling "thirty
  eight times a second" (`xbox_memory_layout.c:810-816`). The ring
  (40 ms) plus 43 ms device pulls leave little slack if refill
  wakes halve.
- **FMV**: the Bink player lives in `src/video/video_pump.c` and its
  frames reach the screen through the same flip/present path —
  flagged as a proof checkpoint in §4, not analysed further here.
  **[desk]**

## 2. What "half rate" would even mean here — the candidate designs

### A. Present-divider — render every frame, show every other one

*Expressible:* yes, trivially — skip the present (or the whole flip)
on alternate flips inside `dk_flip`/`present()`
(`nv2a_dk.c:5069, 3232`). The presenter re-blits the last surface
each time, so "show the previous picture again" is free.

*Consequences:*
- Guest logic, timers, animation: **untouched** — everything still
  runs at the vblank rate. Full game speed. ✓
- Input: still polled at 60/s, but every other poll's result is
  never displayed; input-to-photon latency grows by up to one
  displayed frame (~16.7 ms). ✗ (mild)
- Cost: **saves nothing.** The executor walk (~15.3–15.9 ms/frame)
  and the renderer still run for every frame **[campaign: F1 — the
  whole draw path is only ~3 ms of the frame and mostly
  overlapped]**. On a frame-rate option whose purpose is relief in
  heavy scenes, this is the fatal flaw:
- In a heavy scene the guest already produces at ~33 ms. Dividing
  *that* yields a ~66 ms display cadence — the option is unusable
  exactly where it would be wanted, unless the divider is
  rate-aware (engage only when frames arrive faster than the cap).
- Audio: unaffected (device-clocked ring; refill wakes unchanged). ✓

### B. Logic-divider via the vblank — `RECOMP_VBLANK_HZ=30`

*Expressible:* yes — **it already exists as an environment lever**
(`kernel_bridge.c:2420-2427`); nobody needs to write code to try it,
which is precisely why it needs a warning label.

*Consequences:* this divides the guest's frame clock itself.
- Everything the title counts in vblanks — frame-stepped animation
  and game logic, the D3D field parity (`kernel_hal.c:806-820`) —
  advances at half wall-clock speed: **slow motion, not 30 fps.**
  The tick's own comment treats the rate as a game-speed probe: "if
  a title's boot sequence is counted in display interrupts,
  doubling the rate halves the boot" (`kernel_bridge.c:2413-2417`).
- Anything the title times from KeTickCount keeps **wall time**
  (`kernel_bridge.c:225`) — so the two guest clocks disagree by
  2×, and whichever subsystems mix them (a timer that waits N
  vblanks but animates by elapsed tick, say) tear in ways that
  can't be enumerated from the host side. **[desk — the mix is
  title-internal]**
- Input polls halve to 30/s: +16.7 ms latency and coarser input,
  permanently, not just on screen.
- Audio: the ADX refill wakes halve (~38/s → ~19/s) against a
  device that pulls every ~43 ms and a 40 ms ring
  (`apu_core.c:522-545`, `xbox_memory_layout.c:810-816`) — the
  exact coupling whose breakage produced the historic music fault.
  Underrun risk is structural, not hypothetical. **[desk]**
- Verdict: a game-speed control. It must never be what a player's
  "30" row does.

### C. Render-decimation — skip the renderer's work on alternate frames

*Expressible:* yes — the queue consumer could drop a frame's draw
records and still flip (the presenter blits the previous surface,
`nv2a_dk.c:3266+`). No such code exists today (§1c).

*Consequences:* guest logic/input/audio all as in A; saves only the
renderer CPU + GPU of skipped frames — the small, overlapped part
of the frame **[campaign: F1]**. The expensive part (the executor
walk) is upstream of the queue and untouched. Displayed motion is
still a 60-step simulation sampled at 30 Hz, so pans judder the
way any 2:1 sample does. Worst value of the three.

### D. True logic decimation — guest advances two steps per shown frame

**Not expressible.** The guest is recompiled fixed code with a
one-step-per-vblank loop; no host mechanism can make one guest
frame advance two steps. Every honest "30" for this engine is a
*display* cadence (A, done right in §4) — the simulation stays at
60 and the screen shows it at an even 30, exactly as a 30 Hz
display would. Anything promising more than that is secretly B.

## 3. Where the existing naive cap goes wrong

The shipped "30" is design A's crude cousin: it doesn't skip
presents, it *sleeps* in front of them. Four concrete failures,
all **[source]** unless noted:

### 3.1 It paces the queue, not the game

`fpscap_throttle`'s own header comment: the cap is "present-path
state only — nothing in the queue thread's inner loop reads it"
(`nv2a_dk.c:3073-3076`). The sleep sits inside `present()`
(`nv2a_dk.c:3259`), on the renderer queue thread. The guest never
sees a 30 Hz anything; it sees the 2-flip queue limit
(`nv2a_backend_queue.c:628-639`) stop draining — production stalls
in bursts (fill two flips, block, wake on a present, repeat)
instead of flowing at an even rate. A stall is not a clock.

### 3.2 Its deadline resets every frame, so overshoot compounds

The throttle computes `earliest = s_last_present + period`, sleeps
once with `svcSleepThread`, then sets **`s_last_present = now` —
the post-sleep time** (`nv2a_dk.c:3089-3106`). Every sleep's
overshoot is therefore folded into the next frame's budget,
forever. The GL backend's `frame_pace` documents this exact
failure in its own history — a restarted clock turned "a 16.7 ms
period into a 19.5 ms one and a 60 fps cap delivered 51" — and
fixes it with a deadline that *advances by one period* regardless
of when the sleep ended, phase-eased toward the measured display
refresh, with the last millisecond spun rather than slept
(`nv2a_gl.c:11717-11800`). **None of those refinements were ported
to the DK throttle.** The Switch cap is the GL cap as first
drafted, bugs included.

### 3.3 Its phase is unrelated to either clock it must agree with

Guest frames *complete* on vblank boundaries (the loop waits on
the vblank event; frame times quantise — F6). The throttle
releases presents on a free-running wall-clock phase that starts
whenever the first present happened. With `RECOMP_VSYNC=0` the
panel adds no quantisation of its own to rescue the alignment.
Two ~30 ms cadences with unrelated phases beat: displayed
intervals alternate long/short (the classic 33/50 pattern in
quantised data) instead of sitting at 33.3. **[desk mechanism,
directly testable by the §4 rig]** In light scenes the same
coupling can degenerate the other way — loop period = throttle
period with one logic step per loop = uniform-but-half-speed —
and only the hardware signature distinguishes which failure a
given scene gets (§4).

### 3.4 It taxes everything in the present path, loading included

There is no pre-scene exemption: the throttle runs on every
`present()`, including the loading-screen loop
(`nv2a_dk.c:3436, 3259`). GL exempts pre-scene frames precisely
because "this title's loader is driven by its frame loop, so
capping the frame rate caps the loading with it"
(`nv2a_gl.c:~11730-11760`). At 30, DK loading presents (and, via
backpressure, the guest's loading frames) are throttled too.
Also: the sleep is inside `dk_flip`'s timed region
(`nv2a_dk.c:5074-5078`), so the [perf] report's "present" figure
absorbs the sleep and muddies the very measurement you'd check
the cap with; and the menu, drawn inside present (§1b), redraws
at the capped rate while the cap is on.

*Checked and NOT a failure:* the long throttle sleep does not
starve vblank acknowledgement — the keeper thread completes
vblank acks and moves KeTickCount at ~1 ms cadence independently
(`xbox_memory_layout.c:908-960`). The historic lock-hog failure
mode does not recur through this path.

*Status note:* the campaign's frozen options held `fps=60`
throughout, so **the current 30 has no hardware signature on
record** — Brandon's judder report is firsthand, and §3 is the
source-level explanation of it. Capturing the real signature is
step 0 of the fix work (§4).

## 4. The fix design

### 4.1 Redefine the feature honestly

A correct FRAME CAP 30 for this engine is: **an even 33.3 ms
display cadence at full game speed** — design A's cadence with
§3's failures engineered out. It buys smoothness (locked, even
frames in scenes that wobble between 16.7 and 33.3) and a cooler,
quieter handheld; it does not and cannot buy back the executor
walk. The row's help text should say so. Game-speed halving (§2B)
stays a developer lever, never a player row.

### 4.2 Where it hooks — two phases

**Phase 1 (the minimum correct fix): rewrite `fpscap_throttle`**
(`nv2a_dk.c:3089-3106`), keeping the menu row and latch plumbing
exactly as they are:

1. **Advancing deadline, never reset** — port the GL discipline
   (`nv2a_gl.c:11780-11800`): deadline += period each present;
   overshoot costs only itself.
2. **Phase-lock to the guest's clock** — for a cap of 30 against a
   60 Hz vblank, anchor the deadline grid to the vblank tick:
   presents release on every Nth vblank boundary. The counter for
   this already exists and is exported for the purpose
   (`g_vblank_count`, `kernel_bridge.c:2393-2396`); the tick's
   next-deadline is internal to kernel_bridge and would need a
   small accessor. This kills the §3.3 beat by construction —
   there is only one cadence left in the system.
3. **Sleep in slices, spin the tail** — replace the single
   `svcSleepThread` with ≤1 ms slices (the keeper already protects
   vblank acks, but slices keep this thread's own wake latency
   honest) and a short spin for the final fraction, per GL.
4. **Pre-scene exemption** — no cap until the first real scene
   frame (GL's draw-count heuristic, `nv2a_gl.c:~11730-11760`),
   so loading is never throttled.
5. **Rate-aware disengagement** — the divider engages only when
   presents actually arrive faster than the cap; at the 99th
   Street patrol (native ~33 ms) it must be a no-op, not a 66 ms
   divider. (In deadline terms this is automatic — a late frame
   just misses its slot and takes the next — but the proof must
   demonstrate it.)
6. **Move the wait out of the timed region**, or account it
   separately in [perf], so the instrument stays clean (§3.4).

*Interaction with `RECOMP_VSYNC=1`:* when the swapchain itself
waits for refresh, presents already quantise to the panel; the
divider should then count swapchain presents (every other one)
rather than fight the interval with sleeps. One master clock per
configuration — vblank grid when vsync is off, refresh count
when it's on. **[desk — needs a design pass at build time]**

**Phase 2 (the architecturally pure site — later, if ever):**
make the flip stall itself complete on the Nth vblank. The
executor's comment asks for exactly this — "pacing wants the
vblank clock in the kernel, not a sleep in here"
(`nv2a_pb_exec_dk.c:141-145`) — and the stall machinery exists
(`NV097_FLIP_STALL`, `nv2a_pb_exec_dk.c:145`). Guest production
itself would even out and the queue backpressure would stop
being the coupling. It is also the riskiest surface in the
renderer (the same comment block records a loader that hangs
forever if a flip stall never clears), so it is a phase-2
candidate only, behind phase 1 proving the concept.

### 4.3 Risks register

- **Input latency:** +up to one displayed frame (~16.7 ms) at
  cap 30 — inherent to any present divider; polls stay 60/s.
- **Menu at 30 Hz** while the cap is on (menu draws in present).
  Acceptable; note it in the row's help text.
- **FMV** shares the flip path — Bink content is typically
  ≤30 fps native, but its presentation cadence under the divider
  is **unproven**; a proof checkpoint, below.
- **Audio:** no mechanism touches it (device-clocked ring;
  vblank rate unchanged). Any audio regression in proof means
  the implementation leaked into the wrong layer.
- **The 17 ms vblank period** (§1a): the "60 Hz" grid is really
  58.8 Hz in whole-ms rounding. A vblank-locked 30 is therefore
  ~34 ms, evenly — fine, but the proof's expected cluster centre
  is 33–34 ms, not 33.30.

### 4.4 How the proof runs (hardware)

**Step 0 — capture the current 30's signature** (same rig, no new
code): shipped build, `fps=30` via options, two dwells — (a) a
light scene where the guest holds 60 natively (garage), (b) the
standard 99th Street patrol. Instruments already in the tree:
`RECOMP_DK_HITCH_MS=17` hitch log (the F6 instrument — it logs
every present-to-present interval over threshold, and at cap 30
*every* frame qualifies), the [perf] report's present figure,
and `g_vblank_count` vs the renderer's frame count over a timed
window.

Expected current-build signatures **[desk predictions]**:
- Judder: hitch intervals bimodal/scattered (16.7 + 33 + 50 ms
  mixtures) in the light dwell; game timers at full speed.
- Or slow-motion degeneration: uniform ~33 ms intervals but a
  timed in-game event runs at half wall speed.
Either confirms §3 and identifies which failure dominates.

**Step 1 — phase-1 build, same dwells. PASS requires ALL of:**
- Light dwell: present intervals tightly clustered at 33–34 ms;
  **zero** 16.7 ms and zero 50 ms hitch lines across a 5-minute
  dwell; vblank:present ratio 2.00:1 sustained over the window.
- Game speed: a timed in-game event (a song's known length, a
  graffiti timer) matches wall clock within ~1% — proves the
  vblank was not divided.
- Patrol dwell: baseline unharmed — presents stay at the native
  ~33 ms median (divider demonstrably a no-op at native ≤30
  arrivals), pooled median within noise of the 33.0 ms campaign
  baseline.
- Audio: music continuous through both dwells; no keeper-stall
  log lines (`xbox_memory_layout.c:940-945` prints them).
- FMV checkpoint: one Bink sequence played at cap 30 — plays at
  speed, no stutter step.
- Menu: opens, navigates, and the Frame rate row switches
  60 ↔ 30 live with the cadence change audible/visible
  immediately (the latch path at `nv2a_dk.c:3148,3181` already
  supports live changes).

**FAIL signatures and what they'd mean:** bimodal intervals →
phase lock failed (back to §4.2.2); uniform intervals + slow
game clock → the vblank got divided (wrong layer — §2B leaked
in); patrol median doubles → rate-aware disengagement missing.

## Appendix — one-paragraph summary for the menu work

The port paces the guest with an emulated 60 Hz vblank raised by
the kernel timer thread; the renderer presents whenever the guest
flips, through a 2-deep queue. The existing Frame rate row's 30
is a wall-clock sleep in the present path whose deadline resets
every frame and whose phase ignores both the vblank and the
panel — it throttles the queue rather than pacing the game, and
the two unmatched cadences beat into judder. The fix is a
vblank-phase-locked, advancing-deadline present divider in the
same hook (the GL backend's `frame_pace` is the in-tree
reference for every discipline the DK throttle lacks), with a
pre-scene exemption and rate-aware disengagement — a display
cadence at full game speed, which is the only honest "30" this
engine admits.
