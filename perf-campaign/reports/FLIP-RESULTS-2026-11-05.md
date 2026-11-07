# LIVE COMPARE flip campaign — results (2026-11-05)

Coordinator run of the sheeted campaign in FLIP-MATRIX-2026-11-05.md /
ROUTE-SCRIPT-2026-11-05.md, executed with the PC-side driver
(`~/flip/drive.py` on the build PC WSL; VM copies in `flip-work/`) under the
console claim protocol (claimed 09:40 NST and held for the day). All numbers come from
`extract-2026-11-05.py` over the actual run logs. GPU figures are
**timestamp-reported** throughout: C1's fence test could not settle
the ×1.6276 question on this scene (fence wait 0.0 ms in all 255
blocks — the GPU drains inside the CPU frame), while mvp-nx
independently documents the ×1.627 factor as real on the same
deko3d stack (see the C1 addendum). Read GPU numbers as
timestamp-reported, with a strong prior that true GPU time is
~1.63× higher; no frame verdict in this record depends on them.

## Method as executed

- Shipped release build (v0.4.0-switch, NRO 44,865,733 B, SHA-256
  e3bfa433…), forwarder title on stock clocks by Horizon OC pin;
  the clock gate is enforced per run from its own [SWPERF] samples.
- Route: the sheeted run-56 protocol as automated by the driver —
  load Game Data 1, garage departure, then in 99th Street the patrol
  pattern FWD 14 s / BACK 28 s / FWD 14 s × 4 cycles (no jump tokens,
  per the coordinator decision recorded pre-campaign), executed at the
  99th entry plaza facing the big tower. Brandon, watching live,
  confirmed that plaza back-and-forth is a legitimate heavy-load
  segment ("that tower view is the worst (heaviest) part of 99th") —
  it is not run 56's street route, and same-night comparisons are
  internal to this pattern (buckets condition on draws/frame).
- Motion probes: base-r1 (only) carried a per-cycle motion probe —
  the first 2.5 s of each FWD leg skated between two captures — added
  when a fall was reported mid-calibration. Brandon then corrected
  that report (the character was upright on a plain line the whole
  time; see the calibration section), the probe machinery was removed
  from the driver for base-r2 onward, and no segment in this campaign
  is discounted for any fall. The probes are ordinary forward skating
  plus screenshots; they do not change a run's inputs.
- Per-run discipline per FLIP-MATRIX §3.2: options installed from the
  baseline manifest with byte-exact read-back before every boot,
  baseline restored and read back after every run, settings hash
  verified after every session (246 B, SHA-256 92e6aa90… throughout).

## Setup (§3.0)

- Prenight options snapshot (as found, = end-of-campaign restore
  target): 6,008 B, SHA-256 1c2a2772…
- Tonight's baseline manifest (as-found minus the one extra line
  `RECOMP_DK_GPU_SLOW_MS=0`; shipped CRLF bytes preserved): 5,983 B,
  SHA-256 52ed87b4… The coordinator's 5,832 B / 5a950a9c… manifest from
  the previous night differs only by line-ending normalisation; the
  effective key set is identical (frozen six present, all flip lines
  absent), verified line by line.

## Calibration run — flip-20261105-cal (NOT a baseline)

Purpose: driver proof + route calibration. Baseline options.

- Validity gates: clocks PASS (13/13 [SWPERF] samples 1020/384/1331),
  purity PASS (Stg30 ×1, MAP/TRACK/CUSTOM 0), version 0.4.0-switch.
- Segment: 279.6 s in-area (over the 270 s ceiling — departure trimmed
  from 5 bursts to 3 for all subsequent runs), 6,308 frames, avg 22.6 fps.
- Combined 400–699 median **33.0 ms** (sub-buckets 33/34/39, n=2,975);
  fit 19.35 ms + 30.6 µs/draw; walk 19.3 µs/draw; renderer draws
  10.3 µs/draw; GPU median 8.80 ms timestamp-reported; draws/frame
  median 828 (the plaza/tower pattern is far heavier than run 56's
  street route: 467 draws/frame, buckets to 1000–1099 populated).
- Why it is not base-r1: the segment runs 279.6 s, over the sheeted
  270 s ceiling, and the run used the pre-trim 5-burst departure —
  the route script's discovery-run rule also designates it discarded.
  Its numbers above stand as calibration data. (A mid-run report that
  the skater had fallen was corrected by Brandon afterwards: he was
  upright, skating a plain line back and forth facing the tower —
  valid heavy dwell, as approved. Nothing in this run is discounted
  for any fall.)

## Takeover note — successor coordinator, 2026-11-05 ~10:37 NST

The first coordinator terminated mid-campaign without a final report.
Its claim (the claim file, held since 2026-11-05T09:40:14-03:30) was still on the card, and its last run
(base-r3, attempt 1) died just after the load chain fired, leaving the
game running in the garage. The successor probed read-only first
(`game name` = JSRF Unleashed NX — the predecessor's own orphaned
session, no foreign session; card options = tonight's baseline
manifest 52ed87b4…, settings 246 B / 92e6aa90…, shipped NRO resident
44,865,733 B), then re-claimed under the same campaign ownership for
continuity (new since timestamp, takeover noted here per the claim protocol). Inventory at takeover: base-r1 and base-r2 were
complete on the LOQ with logs pulled (extracted below); base-r3
attempt 1 is VOID (see Voided). The campaign resumes at base-r3.

## Schedule note — recorder blueprint run this evening (Nov 5)

Brandon will play a recorded blueprint run of 99th Street this
evening on a separate recorder build (being built from
RECORDER-SCOPE-2026-11-05.md by another worker). It needs one
console claim window this evening to swap its NRO in and let him
play; it queues behind this campaign per the claim protocol.
Plan consequence for the campaign: console batches today are
scheduled to finish and free the console before the evening —
game closed, shipped NRO resident, claim released — so the
recorder window can claim cleanly. The recording becomes a
campaign asset afterwards; integrating replay-driven runs is a
later step, NOT today's. The matrix continues on the patrol
pattern exactly as sheeted.

## Same-night baseline

- **base-r1 — VALID.** Clocks PASS (12/12 [SWPERF] at 1020/384/1331),
  purity PASS (segment MAP/TRACK/CUSTOM 0), version 0.4.0-switch,
  segment 262.6 s, 6,234 gameplay frames. Combined 400–699 median
  **33.0 ms** (sub-buckets 33/34/39, n=2,819); fit 19.37 ms +
  30.6 µs/draw; walk 19.3 µs/draw; renderer draws 10.2 µs/draw;
  GPU median 8.50 ms timestamp-reported; draws/frame median 825.
- **base-r2 — VALID.** Clocks PASS (12/12), purity PASS, segment
  257.8 s, 6,146 gameplay frames. Combined 400–699 median
  **33.0 ms** (sub-buckets 33/34/39, n=2,836); fit 19.51 ms +
  30.3 µs/draw; walk 19.4 µs/draw; renderer draws 10.4 µs/draw;
  GPU median 8.40 ms timestamp-reported; draws/frame median 818.
- **base-r3 — VALID** (successor, valid on its first held-session
  attempt after two VOID attempts; see Voided). Clocks PASS (12/12),
  purity PASS, segment 257.8 s, 6,146+ gameplay frames. Combined
  400–699 median **33.0 ms** (sub-buckets 33/34/39, n=2,832); fit
  19.61 ms + 30.3 µs/draw; walk 19.5 µs/draw; renderer draws
  10.3 µs/draw; GPU median 8.10 ms timestamp-reported; draws/frame
  median 817; scale x1.50 dynamic in all 129 segment blocks.
- **Pooled baseline (r1+r2+r3): combined 400–699 median 33.0 ms**
  (runs 33.0 / 33.0 / 33.0 — span 0 ms, variance gate passed with
  no extra runs). All verdicts below read against this.
- Baseline [gpu] slow-frame lines in segment: **0** in cal, base-r1
  and base-r2 (the only [gpu] frame line in each log is a single
  pre-segment frame; extraction helper `flip-analyze.py`). Per the
  matrix, F7 is therefore a formality — the first cut if time runs
  short.
- Baseline scale: [perf] reads **x1.50 dynamic in every segment
  block** of cal/r1/r2 — scale=auto sits pinned at handheld max on
  this pattern. Baseline texture-check sub-read (medians): 0.25 ms,
  28 checks/frame, ~955 put off. Baseline alpha mix (medians):
  off 11%, cannot-fail 0%, proved-dropped 18%, no-depth-writes 18%,
  depth-writes 51%. Fence wait ("waiting for the GPU"): 0.0 ms
  median/p90/max across all baseline segment blocks.

## Flip configurations

### F2 — NO ALPHA TEST (`RECOMP_DK_EZ_TEST=1`) — 3 valid runs

Latch proven in all three (log line `RECOMP_DK_EZ_TEST=1: every
draw without the alpha test`, ×1 per run; options header carries
the line). All clocks PASS, purity PASS, scale x1.50 dynamic
throughout (no dyn confound).

| Run | Combined 400–699 | Sub-buckets | GPU med (ts-rep) | GPU p90 |
|---|---|---|---|---|
| f2-ez-r1 | 33.0 ms (n=2,984) | 33/34/39 | 6.9 ms | 7.8 |
| f2-ez-r2 | 33.0 ms (n=2,827) | 33/34/39 | 6.8 ms | 7.8 |
| f2-ez-r3 | 33.0 ms (n=2,829) | 33/34/39 | 6.7 ms | 7.8 |

Pooled F2 combined median 33.0 ms vs baseline 33.0 ms → **gain
0.0 ms**. Walk (19.4–19.6 µs/draw) and renderer draws component
(10.3–10.5 µs/draw) unchanged, as predicted — the alpha test's
cost is GPU-side: GPU timestamp-reported fell ≈1.6 ms (8.4 → 6.8
pooled medians), inside the sheeted −0.5 to −2 ms hypothesis band.

**Verdict (pre-registered mirror rule, §0.5): the total
alpha-test price on frame time is < 1 ms — the component is
exonerated as a frame-time lever at scale=auto (x1.50).** Its
~1.6 ms GPU cost is real but not pacing: the frame is CPU-serial
(walk + renderer), the GPU has headroom, so removing the test
buys resolution budget, not frame time. §8 lever 2's frame-time
headroom via alpha-test removal is nil on this evidence. F2 is
measurement-only, never a shipping configuration.

### F3 — ALPHA-TEST PROOF off (`RECOMP_DK_AT_PROVE=0`) — 3 valid runs

Latch proven in all three (`are dropped: off`, ×1 per run). The
pre-registered alpha signature moved exactly as sheeted:
proved-dropped **18% → 0%**, on-with-depth-writes 51% → 69–70%.
All clocks PASS, purity PASS.

| Run | Combined 400–699 | GPU med (ts-rep) | Scale |
|---|---|---|---|
| f3-atprove0-r1 | 33.0 ms (n=2,830) | 7.3 ms | x1.25 dynamic |
| f3-atprove0-r2 | 33.0 ms (n=3,009) | — | x1.25 dynamic |
| f3-atprove0-r3 | 33.0 ms (n=2,833) | — | x1.25 dynamic |

**Frame verdict: regression 0.0 ms** (33.0 vs pooled baseline
33.0) — under the sheeted +0.5 to +2 ms hypothesis; by the F3
mirror rule the proved-skip's frame-time value is **< 1 ms: it is
cheap correctness insurance, not a frame lever.**

**The scale confound fired, and it is the real F3 finding:**
every F3 segment block reads **x1.25 dynamic**, one dyn step
below the baseline's x1.50 (quoted with the difference attached
per §0.3; one step does not void). With the proof off, GPU
pressure from the newly-tested depth-writing draws rose enough
that dynamic scale surrendered a resolution step — while frame
time never moved. So the proof's measurable product tonight is
not fps: **AT_PROVE is what holds dynamic scale at x1.50.** It
buys resolution headroom on the GPU side, and F2 already showed
that GPU headroom does not convert to frame time in this build.

### F4a — TEXTURE CHECK EVERY=1 — 3 valid runs

No latch line exists for F4 (per §3.4); proof is the options
read-back (drive log, exact one-line diff) plus the sub-read,
which moved exactly as pre-registered, in all three runs:

| Read (segment medians) | Baseline (=4) | F4a (=1) |
|---|---|---|
| texture checks | 0.25 ms | 0.99–1.00 ms |
| checks/frame | 28 | 111.5–112 |
| put off | ~955 | 0 |

Combined 400–699: **33.0 / 33.0 / 34.0 ms** (run span 1 ms, no
extra runs per §3.5). The +0.75 ms of check cost is visible in
the renderer draws component (10.3 → 11.3 µs/draw) and was
absorbed by renderer-thread idle (77% → 75%): frame effect
≤ 1 ms, inside the sheeted +≤0.5 ms expectation's noise. The
dose–response verdict waits on F4b (=8) below.

### F4b — TEXTURE CHECK EVERY=8 — 3 valid runs

Proof as for F4a (options read-back + sub-read). All clocks
PASS, purity PASS, scale x1.50 dynamic throughout.

| Read (segment medians) | Baseline (=4) | F4b (=8) |
|---|---|---|
| texture checks | 0.25 ms | 0.13 ms |
| checks/frame | 28 | 14 |
| put off | ~955 | ~1,114 |

Combined 400–699: **33.0 / 33.0 / 33.0 ms** — flat.

**F4 dose–response verdict (F4a + baseline + F4b; F4c pending):**
the texture-check sub-read is cleanly monotone in the setting
(=1: 0.99–1.00 ms and 112 checks/frame and 0 put off → =4:
0.25 ms / 28 / ~955 → =8: 0.13 ms / 14 / ~1,114) while frame
medians are flat at every point. **The settled-texture check is
priced — ≈0.25 ms/frame of renderer CPU at the shipped default
of 4, ≈1.0 ms at =1 — and exonerated as a frame-time lever:**
the cost is absorbed by renderer-thread idle (75–77%) at every
setting, so no TEX_EVERY value buys frame time on this pattern.
No visual staleness anomalies were observed on the F4b runs'
captures (the sheeted =8 risk shape did not materialise).

### F4c — TEXTURE CHECK EVERY=2 — 3 valid runs (coordinator #3, on v0.4.1-switch)

Run after the campaign resumed under coordinator #3, on the
landed v0.4.1-switch build (211f4778…) — the first flip measured
across the build change. Proof as for F4a/F4b (options read-back
+ sub-read). All clocks PASS, purity PASS, scale x1.50 dynamic
throughout; attempts voided on 307 boots: r1 ×1, r2 ×2, r3 ×0
(logs stashed as -voidK).

| Read (segment medians) | Baseline (=4) | F4c (=2) |
|---|---|---|
| texture checks | 0.25 ms | 0.50 ms |
| checks/frame | 28 | 56 |
| put off | ~955 | ~640 |

Combined 400–699: **33.0 / 33.0 / 33.0 ms** (sub-buckets
33/34/39 in all three) — flat, and the runs' fit (19.6–19.7 ms
+ ~30 µs/draw), walk (~19.4 µs/draw), renderer draws
(~10.5 µs/draw) and GPU median (8.4 ms timestamp-reported) all
sit inside the morning's v0.4.0 baseline envelope.

**F4 dose–response, FINAL (all four points):** =1: 0.99–1.00 ms
/ 112 checks / 0 put off → =2: 0.50 ms / 56 / ~640 → =4:
0.25 ms / 28 / ~955 → =8: 0.13 ms / 14 / ~1,114. Cleanly
monotone across the full range with frame medians flat at
33.0 ms at every point. The F4a/F4b verdict stands unchanged
and is now complete: **the settled-texture check is priced
(≈0.25 ms/frame at the shipped default) and exonerated as a
frame-time lever.** F4c also closes the interpolant the matrix
kept against a non-monotone surprise — there was none.

### base-d1 — drift check (coordinator #3, on v0.4.1-switch) — NO DRIFT

The mid-campaign drift check, run early per the takeover plan
because the shipped build changed underneath the campaign
(v0.4.0 → v0.4.1, menu skin). Two attempts voided on 307 boots
before it (one under the original B4 batch, one under the
relaunched chain; see Voided). The valid run: clocks PASS,
purity PASS, version 0.4.1-switch, segment 257.2 s, scale
x1.50 dynamic in all 128 blocks.

Combined 400–699 median **33.0 ms** (n=2,824), sub-buckets
33/34/39 — identical to the pooled v0.4.0 baseline. Fit
19.68 ms + 30.3 µs/draw; walk 19.33 µs/draw; renderer draws
10.24 µs/draw; GPU median 8.4 ms timestamp-reported; texture
sub-read at the =4 default signature (0.25 ms / 28 / ~959),
proving the baseline options. **Verdict per §3.1: within 2 ms
of the pooled baseline — no drift; base-d1 pools in** (four
baseline points now: r1/r2/r3/d1, all 33.0 ms). The v0.4.1
build change does not move the frame on this pattern, and the
morning's baseline remains the anchor for every verdict that
follows.

### F1 — DRAW NOTHING (`RECOMP_DK_NODRAW=1`) — 3 valid runs (coordinator #3)

**Method note.** F1 cannot be driven by the standard driver:
with draws suppressed the title menu never renders, so
drive.py's pixel gates cannot see it (the first B5 batch's six
attempts all aborted "menu never opened" on a healthy game —
see Voided). Coordinator #3 derived `drive-blind.py` from
drive.py with the two gated phases replaced by the fixed
sequences the gated runs converged on (PLUS ×2, one 150 ms
DDOWN, then the unchanged load chain); success is judged from
the run log, as the matrix prescribes for F1. Latch proven in
every valid run (`RECOMP_DK_NODRAW: draws counted, not drawn`).
Voids before the valid set: r1 ×2 (one 307 boot with the route
proven, one 307 boot that also missed the load), r2/r3 clean.

**Data-shape caveat.** Under NODRAW every `[hitch]` line reads
"0 draws" (the counter is the renderer's drawn counter), so
the draws-bucketed staircase — and with it the combined
400–699 headline — cannot be built for F1. The reads below are
the like-for-like statistics that survive: the **all-hitch
population** (every segment hitch frame, same extraction for
baseline) and the `[perf]` absolutes. Route match, in place of
the draws gate: segment walk 15.3–15.4 ms/frame vs baseline
15.9 (−3.5%, inside ±15%) and segment frames 5,925–6,059 vs
baseline 5,803–5,924 — the blind runs skated the same route.

| Read (segment medians) | Baseline (r1/d1) | F1 r1 | F1 r2 | F1 r3 |
|---|---|---|---|---|
| all-hitch median | 43 ms | 40 | 40 | 41 |
| all-hitch p90 | 51 ms | 48 | 48 | 49 |
| segment avg fps | 22.6 | 24.0 | 24.1 | 23.6 |
| walk ms/frame | 15.9 | 15.35 | 15.4 | 15.3 |
| renderer CPU ms/frame | 9.3–9.4 | 0.7 | 0.7 | 0.7 |
| draws component ms | 8.4–8.5 | 0.1 | 0.1 | 0.1 |
| GPU median (ts-rep) | 8.4–8.5 | 0.1 | 0.1 | 0.1 |
| renderer idle | 77% | 97% | 97% | 97% |

**Verdict (matrix §1 F1, pre-registered): the fall is ≪ the
hypothesised 4–6 ms.** Deleting the *entire* renderer draw
segment — 8.7 ms/frame of renderer CPU and all GPU draw work —
buys ~3 ms at the all-hitch median (43 → 40) and ~+1.4 fps
(22.6 → ~24.0). The renderer draw work was therefore mostly
**overlapped, not pacing**: it ran inside the renderer thread's
77% idle while the serial guest → walk → renderer chain set
the frame, and the walk (15.3–15.9 ms/frame, untouched by this
flip, exactly as predicted) is the frame's centre of mass.
Consequences, per the matrix's own terms: every renderer-draw
lever (F2, F3, F4) has a collective ceiling of ~3 ms at the
median — consistent with their individual 0.0 ms frame reads —
and the campaign's remaining upside sits in **§8 lever 1 (the
executor walk)** and the intercept, not in the draw path. F1
is measurement-only, never a shipping configuration.

### F5 — RENDERER TRACE (`RECOMP_DK_TRACE=1`) — 3 valid runs (coordinator #3)

Latch proven in all three: 373–375 `[DK]` surface/texture
creation lines per run (the drive-log latch count), against 3
unconditional boot lines in baseline logs. All clocks PASS,
purity PASS, scale x1.50 dynamic throughout, no voids needed.

Combined 400–699: **33.0 / 33.0 / 33.0 ms** (sub-buckets
33/34/39 in all three) — an exact null against the pooled
baseline, inside the sheeted 0 to +0.3 ms expectation.

**Verdict: TRACE's logging overhead is certified nil on this
pattern.** The creation bursts that do occur (a few hundred
lines across a run, concentrated at area entry) cost nothing
measurable on the frame. "Turn TRACE on and send the log" is a
free support instruction, as a clean null here was meant to
establish.

### F6 — HITCH LOG THRESHOLD 17 (`RECOMP_DK_HITCH_MS=17`) — 3 valid runs (coordinator #3)

Latch proven in all three (`[hitch] lines for frames over
17 ms`). All clocks PASS (r1 needed two retries on 307 boots),
purity PASS, scale x1.50 dynamic throughout. Per the matrix's
validity note, F6's statistics are built from its hitch lines
**subset to frame > 25 ms** (its raw population is a different
instrument); the raw counts are reported alongside.

| Read | Baseline | F6 r1 | F6 r2 | F6 r3 |
|---|---|---|---|---|
| combined 400–699 (>25 subset) | 33.0 ms | 33.0 | 33.0 | 33.0 |
| sub-buckets (>25 subset) | 33/34/39 | 33/34/39 | 33/34/39 | 33/34/39 |
| raw segment hitch lines | ~6,120–6,376 | 6,148 | 6,148 | 6,129 |

**The 17–25 ms band is empty on this pattern:** a direct count
of segment hitch lines by frame time finds no frames at all
between 17 and 29 ms in either F6 or baseline logs — frame
times quantise to vsync multiples (16.7 ms frames are never
logged; the next population sits at ~30 ms+). The widened
threshold therefore added essentially no lines here (raw
counts match baseline's), and the >25 ms subset reads are
identical to baseline on every column (walk ~19.3 µs/draw,
renderer ~10.2 µs/draw, GPU 8.3–8.4 ts-reported).

**Verdict: certified null, as sheeted.** The hitch log — the
campaign's primary instrument — costs nothing measurable at
its higher sensitivity, and on this pattern the sensitivity
increase is theoretical anyway: there is no 17–25 ms frame
population for it to catch.

### base-d2 — late drift check (coordinator #3, on v0.4.1-switch) — NO DRIFT

Clocks PASS, purity PASS, first attempt. Combined 400–699
**33.0 ms** (n=2,868), sub-buckets 33/34/39, fit 19.62 ms +
29.5 µs/draw, GPU median 8.2 ts-reported, scale x1.50 dynamic
throughout. **Verdict per §3.1: within 2 ms of the pooled
baseline — no drift; base-d2 pools in.** The campaign closes
its LIVE COMPARE set with five baseline points (r1/r2/r3/d1/d2)
all at 33.0 ms across two shipped builds.

## YIELD — console handed to Brandon's recorder window, 2026-11-05 12:15–12:21 NST

At 12:15 NST Brandon took a break between classes to record his
blueprint-run inputs (JSRF first, then SPM) and the campaign was
directed to yield. State at the yield: Batch B3 had completed
f4-tex8 r1–r3 (all VALID); f4-tex8-r3 was the slot in flight.
The successor froze the batch runner between slots (SIGSTOP to
the runner shell — the driver itself was untouched), let
f4-tex8-r3 finish its own patrol, close-out and baseline restore
cleanly (drive log: DONE, clocks stock), then killed the runner
so no new slot could start. **Claim released 12:20:50 NST**
(the claim released, absence confirmed by read-back).
Console handed over: game closed, shipped NRO resident
(44,865,733 B — the campaign never writes the NRO), card
options = tonight's baseline manifest (52ed87b4…), settings
246 B / 92e6aa90…. Not yet run at the yield: F4c, base-d1, F1,
F5, F6, base-d2, C1, C2, C3, S2. The campaign resumes when the
claim is handed back after Brandon's recording sessions.

## Successor #2 stint — COLLISION STOP, 2026-11-05 ~14:00–14:04 NST

Successor #2 claimed at 14:00:26 NST (claim verified absent at
13:59 by probe.py; card as-found: options = baseline manifest
52ed87b4…, settings 92e6aa90…, shipped NRO resident, `game name`
erroring "running=0"). Batch B4 (F4c ×3 + base-d1) launched
14:01:44. **The foreground was not the HOME menu: it was the
New Super Paper Bros. launcher title screen** (the SPM port's
launcher, left in the foreground by the preceding SPM session;
the launcher does not register in the test rig's running-game
probe, which is why every pre-flight check read "no game
running"). drive.py's launch logic mistook the screen for a
HOME focus failure and fired its launch + profile-fallback
sequences into the SPM session:

- f4-tex2-r1 attempt 1 (14:01:44): **A** ≈14:01:53 — on the
  launcher's "Press A to start", this **started Super Paper
  Mario NX** — then fallback **B, DDOWN, DUP, A** ≈14:02:03–07
  into the loading/running game. ABORT 14:02:17.
- f4-tex2-r1 attempt 2 (14:02:21): **A** ≈14:02:23, fallback
  **B, DDOWN, DUP, A** ≈14:02:33–37, into the running game.
  ABORT 14:02:48.
- f4-tex2-r2 attempt 1 (14:02:48): **A** ≈14:02:50, fallback
  **B, DDOWN, DUP, A** ≈14:03:00–04. ABORT 14:03:14.
- f4-tex2-r2 attempt 2 (14:03:19): **A** ≈14:03:21, fallback
  **B** ≈14:03:30, **DDOWN, DUP, A** ≈14:03:32–34 (the batch
  was killed mid-fallback at ~14:03:5x; the last tokens may
  or may not have fired — the drive log ends at the fallback
  notice).

The coordinator recognised the foreground from the run's own
00-home capture, killed the batch (no further input after
~14:04), closed nothing, and left the SPM session running
exactly as found: a read-only capture at ~14:05 shows Super
Paper Mario gameplay idle in a level (Mario standing by
barrels; HP 7/10, score 0009700; `game name` = "Super Paper
Mario NX"). **No JSRF run was completed in this stint; no
measurement was taken.** Card restored to the as-found state
and the claim released (both verified): options = baseline
manifest 52ed87b4… (drive.py's cleanup and a final manual
restore agree), settings 246 B / 92e6aa90…, shipped NRO
resident, the claim file absent at ~14:06. The prenight
options (1c2a2772…) were NOT restored: the campaign is
interrupted, not finished, and baseline is the as-found /
yield state. Root cause for the record: the pre-flight guard
probed `game name` only; a leftover homebrew launcher in the
foreground is invisible to that probe. A screen capture must
be part of the fresh-probe guard before first input.
Remaining when the console is genuinely free (SPM session
closed by its owners): F4c, base-d1, F1, F5, F6, base-d2, C1,
C2, C3, S2, then the upstream-queue tail task.

## Coordinator #3 takeover — 2026-11-05 ~15:19 NST (card clock)

Successor #2's stint ended at the collision stop; the console was
then used by SPM's session close-out, Brandon's own recorder and
SMATRS/DBI windows, the build-H menu proof, and finally the
v0.4.1-switch landing smoke (claim `jsrf-landing-v041-smoke`,
15:14:46, released before 15:39). Coordinator #3 probed read-only
first (claim absent after the landing released; `game name`
running=0; **screen capture visually confirmed the HOME menu
with focus on the JSRF tile** — the amended capture guard), then
claimed as `jsrf-flip-campaign` (since 2026-11-05T15:19:35-03:30,
read back and held).

**The shipped build changed during the interruption.** The
landing shipped v0.4.1-switch (menu game-assets skin; tree
3027f69 → 5b0e0ec on the LOQ). Resident NRO at takeover:
44,882,117 B, SHA-256 211f4778… (measured by read-back;
supersedes e3bfa433… as "shipped" from here on). The skin is
menu-code-only and the flip variables all live in the renderer,
but comparability with the morning's pooled baseline (33.0 ms,
measured on v0.4.0) is NOT assumed: base-d1 runs early (it is
the last slot of the first resumed batch) as the drift check
against the build change, and any drift is reported as found.
Card as found: options = baseline manifest 52ed87b4…, settings
246 B / 92e6aa90… — unchanged.

Resumed execution order (matrix order): F4c ×3, base-d1, F1 ×3,
F5 ×3, F6 ×3, base-d2, C1 parks, C2 ×6, C3 ×3, S2 ×3. F7 stays
cut (formality — baseline in-segment [gpu] count is 0).

## Continuations (C1 / C2 / C3)

### C1 — parked-vista fence test (scale=max): INCONCLUSIVE AGAIN (pre-registered gate)

One park session, `flip-20261105-c1-park`, on the landed
v0.4.1 build, first attempt valid: clocks uniformly
1020/384/1331, purity clean (Stg30 present, MAP/TRACK/CUSTOM
= 0), options = baseline + `scale=max` installed byte-exact
(5,981 B, 13a2e72f…), baseline restored after. Three 150 s
parks at the departure-end 99th Street plaza/tower vista,
150 s apart with repositioning between; segment span ~612 s,
255 usable deko3d [perf] blocks after dropping the first 5.

The vista target was met and held: **median 999 draws/frame**
(p90 1,006, max 1,012; the only sub-900 blocks are the two
reposition transients), and every parked block reads
**x1.50, NOT dynamic** — `scale=max` held the top step for
the entire session, as designed.

The fence instrument, however, reads nothing: **fence wait
("waiting for the GPU") median 0.0 ms, p90 0.0, max 0.0 —
0.0 in every one of the 255 parked blocks.** Present median
0.9 ms/frame. The pre-registered validity gate (median fence
wait ≥ 1 ms over parked blocks) therefore FAILS, and the
fence/GPU ratio is 0.000 — outside both verdict bands
(0.85–1.15 / 1.45–1.80).

**Verdict, exactly as pre-registered: C1 is inconclusive
again.** The reason is now measured, not guessed: at this
vista the GPU's timestamp-reported time is 9.8 ms/frame
(median; p90 9.9, max 10.6 — rock-stable across 255 blocks)
against a ~33 ms CPU-bound frame, and the renderer pipelines
two frames ahead, so the GPU's work drains entirely inside
the CPU frame and the present path never blocks on the GPU
fence. Fence wait structurally cannot observe true GPU time
in this engine whenever CPU ≫ GPU; no park duration or vista
choice fixes that. The ×1.6276 timestamp-correction question
is therefore **NOT settled by C1** and stays open; all GPU
figures in this record remain "timestamp-reported". The
follow-up that could settle it is the instrumented
svcGetSystemTick build named in the matrix — out of scope
for this campaign.

One bounded observation that does NOT settle the correction
but bounds its importance: even if the ×1.6276 correction
were real, the corrected GPU figure at the heaviest vista
would be ~15.9 ms/frame — still well under the 33.0 ms
frame. Under either reading of the GPU column, the GPU is
not the frame pacer at 99th Street; no campaign verdict
changes either way.

**Addendum (same night, campaign intel + source check).**
Two later inputs bear on C1's interpretation; neither
changes the verdict above, which stands as pre-registered
(the fence decider was the agreed instrument and it never
engaged):

1. *The correction is independently documented on this
exact stack.* The mvp-nx deep dive (MVP-NX-DEEP-DIVE.md)
found `kNsPerGpuUnit = 1.627` at mvp-nx's
`switch/dk_core.h:44-46`, with the comment that deko3d
converts the 19.2 MHz timer as if it ran at 31.25 MHz —
the campaign's ×1.6276 factor and its mechanism, stated
independently by another project on the same deko3d, whose
published GPU times already include it. The prior for
"the correction is real" is therefore strong; what C1
established is only that the fence instrument cannot
confirm it on this engine, not that it is in doubt.
2. *Present-placement hygiene: ours is clean.* Source
check of `src/nv2a_dk/nv2a_dk.c` (landed tree): the
frame-end timestamp is recorded into the command buffer
at the top of `present()` (dkCmdBufReportCounter,
DkCounter_Timestamp, ~line 3262) **before**
`dkQueueAcquireImage` (~line 3269) — the window closes
before the swapchain acquire wait, the same
submit-before-acquire construction mvp-nx uses. The
frame-start stamp is written at the top of
`frame_begin()` (~line 589, TimestampPipelineTop) and the
pair is read back in frame_begin() after the slot fence,
converted by deko3d's own dkTimestampToNs — the function
whose ns-per-unit constant the 1.627 factor disputes.
So present stalls cannot contaminate our GPU figures or
mimic/mask the correction; if the svcGetSystemTick build
ever runs, a ts-vs-true gap there is attributable to the
timer conversion itself, not to window placement.

### C2 — scale=1, GPU_MS forced: EXACT NULL (cost model stands)

Six valid runs on the landed v0.4.1 build, standard patrol.
C2-control (`scale=1`, GPU_MS absent): combined 400–699
staircase median **33.0 ms** in all three runs (n=2,851 /
2,838 / 2,849); buckets 33/34/39, then 700: 43, 800: 44,
900: 50, 1000: 51; GPU (timestamp-reported) median
5.6/5.5/5.55 ms; every segment [perf] block reads **x1.00
fixed** (never "dynamic"), as pre-registered. C2-flip (same
+ `RECOMP_DK_GPU_MS=8`): combined **33.0 ms** in all three
runs (n=2,835 / 2,848 / 2,842); bucket-for-bucket identical
(33/34/39/43/44/50/51); GPU median 5.55/5.55/5.5; walk
~19.1 µs/draw, renderer ~9.9–10.1 µs/draw, renderer idle
77%, alpha shares, texture checks (~0.24 ms, 28/frame) —
every reported column indistinguishable from control,
including the GPU column itself: forcing GPU_MS=8 did not
move even the figure it nominally sets, at fixed scale.

**Verdict: the pre-registered null.** No ≥3 ms difference
anywhere, nothing to repeat-confirm; the §1 cost model is
not falsified. GPU_MS is not a frame lever on this build.
Voids: control r3 ×1, flip r1 ×1 (both 307 boots, stashed).

Bridge note: C2-control is the scale=1 fixed point toward
run 56 — quoted as a bridge, not a baseline. Its combined
median equals the dynamic-scale baseline (33.0) exactly,
and its heavy buckets (800: 44, 900: 50) match C3's
fixed-x1.50 buckets below: at this vista the frame does
not price resolution at all (see C3).

### C3 — scale=max (x1.50 fixed): frames identical to scale=1; A.1 targets missed on the CPU side, GPU not binding

Three valid runs on the landed v0.4.1 build, `scale=max`,
every segment block **x1.50 fixed** (129 blocks/run).
Combined 400–699 median **33.0 ms** in all three runs
(n=2,818 / 2,846 / 2,825); buckets 33/34/39, 700: 43,
800: 44, 900: 50, 1000: 51–52 — identical to C2-control's
x1.00 staircase at every bucket. The only column that
moves is the GPU's own timestamp-reported median:
5.5 → 8.2/8.1/8.5 ms (+~2.8 ms for 2.25× the pixels),
still under a third of the 33.0 ms frame. Walk ~19.3–19.5
µs/draw, renderer idle 77%, fence wait 0.0 throughout.
Voids: r2 ×2 (307 boots, stashed).

Against the A.1 targets (800–899 bucket ≤16.7 ms; 1,000+
draws ~25 ms): **missed** — 800 bucket reads 44.0 ms and
the 1,000+ bucket 51–52 ms. But the miss is entirely on
the CPU side: the same buckets at x1.00 read the same
numbers, so GPU fill is not what stands between this
build and the targets. Per the pre-registered read, this
is not the "GPU fill binds → dynamic-resolution campaign"
case — it is its inverse. Resolution at 99th Street is
free because the frame is executor-walk-bound (F1's
anchor verdict); a dynamic-resolution phase would buy
nothing here. The remaining upside stays where F1 put
it: §8 lever 1, the walk.

## S2 (runtime at O3): NO FRAME GAIN — walk sub-read −~0.5 µs/draw, sub-quantum; no misbehaviour

Swap per S2-BUILD-2026-11-05.md, with the restore target
corrected to the landed build (parent steer): S2 NRO
(45,316,293 B, e575024b…) PUT to the card and verified
byte-exact by full pull-back before the first launch; after
the runs, the landed v0.4.1 NRO (44,882,117 B, 211f4778…)
was restored and verified the same way. Resident-NRO hash
is the identity proof for every run below (S2's log header
reads 0.4.0-switch / JSRF_GEN_OPT=-O3 by design, same as
shipped). Three valid baseline-protocol patrol runs
(r3 needed one retry — a 307 clock void, stashed); the
first run doubles as the boot smoke: clean boot, clean
patrols, no visual corruption or hangs anywhere — no
-O3-exposed misbehaviour observed.

Frames: combined 400–699 median **33.0 ms in all three
runs** (n=2,819 / 2,832 / 2,885) — identical to the pooled
baseline. Runs r1/r2 are bucket-for-bucket identical to
baseline (33/34/39/43/44/50/52); r3 sits exactly one
quantum lower in five buckets (32/34/38/42/44/49/51) but
its combined median is still 33.0. Scale reads x1.50
dynamic (baseline options), GPU (timestamp-reported)
8.1/8.2/8.3 ms, renderer idle 77–78% — all as baseline.

The one real movement is in the sub-read the experiment
aimed at: executor walk 18.66 / 18.81 / 18.78 µs/draw
against ~19.1–19.5 in the same-night C2/C3 patrols on the
shipped build — about 0.4–0.5 µs/draw (~2.5%) cheaper at
-O3, consistently across all three runs. It does not
cross a vsync quantum at this scene, so it buys no frame
time. **Verdict: the runtime at -O3 is not a frame lever
at 99th Street.** The walk got slightly cheaper and the
frame did not move — the same shape as F4's dose–response:
real sub-read, flat staircase. (Caveat carried from the
build record: neither build gives the generated code
-fwrapv/-fno-strict-aliasing at the runtime level; three
clean patrols are evidence, not proof, of no UB exposure.)

**Shader-pack hygiene (uam sched-word confound — checked
for every A/B in this record).** The mvp-nx deep dive §10
established that our vendored uam lacks the deterministic
sched-word fix: identical GLSL can compile to different
DKSH depending on process history, and the output freezes
into the persistent `dk_shaders.pak`. That confound is
provably ABSENT here: the card's pack (1,417,708 B) was
last appended **2026-11-04 18:18** (card clock) — before
the landing, before every Nov 5 run in this record — and
every [perf] block of every campaign run (patrol flips,
C1–C3, S2, and the replay pass arms) reports **0 shaders
built on the console, 0 new keys recorded**: nothing was
compiled in-run by any arm, so every arm of every A/B
executed the same frozen shader binaries. S2's cross-build
comparison and the patrol-vs-replay comparison are both
pack-clean. (The missing uam patches 6/7/8 are a real
codegen risk for future first-run pack builds; fixing
them is a separate commission, not campaign work.)

## Replay verification pass (Brandon addendum, 2026-11-05 16:19 NST)

Method per the addendum: recorder build (44,869,829 B,
9484e735…, swapped in and verified byte-exact; landed
build restored and verified after), Brandon's blueprint
recording replayed with zero live input (single A launch,
no input at all thereafter), per-run options = the patrol
config + `JSRF_PAD_REPLAY=pad-record.bin` (anchor file
byte-identical to the proof session's: 6,015 B, 68a1b233…).
Two valid runs per config; every run's log trimmed at the
`[PADREC] replay ended` line before extraction, so the
segment is the driven Stg30 run only (game frames
3,899 → 12,059). Clock gate unchanged; voids: F2 r1 ×1,
F2 r2 ×2 (307 boots, stashed).

**Frame-exactness:** all eight runs reproduce the
blueprint's landmarks exactly — Stg00 at game frame 3,584,
Stg30 at 3,899, replay end at poll 12,060 / game frame
12,059. (Flip counts: anchor/F2 runs read 11,723 at replay
end, matching the proof; F3/F4 runs read 11,719 — four
flips fewer by Stg30 entry, a present-timing nuance in the
recorder seam; game-frame landmarks are exact in all 8.)

**ANCHOR (replay-route baseline):** combined 400–699
median **31.0 ms in both runs** (n=2,363 / 2,364);
buckets 200: 25, 300: 26, 400: 29, 500: 34, 600: 37,
700: 40, 800: 44, 900: 48, 1000: 51, 1100: 54. Scale
mixed x1.25 (22 blocks) / x1.50 dynamic. **Patrol-vs-replay
anchor delta: −2.0 ms** — priced and explained: the replay
segment is Brandon's whole-run play (median ~465
draws/frame across mixed 99th Street) against the patrol's
sustained tower-facing plaza loop (~815 draws/frame); the
delta is route/population, not input method. All reads
below are replay-vs-replay only.

**F2 (alpha test off) — patrol verdict CONFIRMED.**
Combined 31.0 / 31.0, buckets anchor-identical; frame
effect 0.0 ms, exactly as on patrol. GPU (timestamp-
reported) median 6.0 → 4.9/5.0 (−~1.0 ms, patrol saw
−~1.6); with that headroom the governor held x1.50 in
every block (anchor dipped to x1.25 for 22 early blocks).
Alpha shares otherwise identical. **Agreement — the
exoneration hardens.**

**F3 (proof off) — patrol verdict CONFIRMED, one flagged
micro-divergence.** The commissioned watch item lands
decisively: the scale surrender **reproduces under
identical inputs** — every block of both runs reads
**x1.25 dynamic** (anchor: mostly x1.50), and the latch
is clean (proved-dropped share 24% → 0). Frames:
31.0 / 30.0 vs anchor 31.0 / 31.0; the renderer sub-read
drops ~1.3 µs/draw (11.2 → 9.8), the proof pass's real
CPU cost, which on patrol converted to no frame time.
Flagged plainly: r2's combined reads 1.0 ms under anchor
with buckets uniformly one quantum lower — a shape patrol
did not produce in three runs (cf. S2 r3's similar dip,
where the combined did not move). It does not change the
verdict — proof-off is not a frame lever at either
route's precision, and its product remains the x1.25
surrender — but it is the pass's one non-null flicker and
it is recorded as such, not smoothed over.

**F4 (texture check =8) — patrol verdict CONFIRMED.**
Combined 31.0 / 31.0, frames flat; sub-read 0.21 → 0.10 ms
and 25 → 13 checks/frame (patrol at =8: 0.25 → 0.13 ms) —
the dose–response's tail reproduced almost to the digit
on a second route and a second build. **Agreement — the
exoneration hardens.**

Pack hygiene for this pass: all eight replay logs report
0 runtime shader builds in every block — same frozen
pack as the patrol arms (see the S2 hygiene note).

## Walk attribution dwell (final matrix item, Brandon commission 2026-11-05 ~20:55 NST)

Full deliverable: **WALK-ATTRIBUTION-2026-11-05.md** in this
folder. Headline: on the landed v0.4.1 build (no new build —
`sample=1` is an options toggle on the shipped binary), one
valid stock-clock patrol run (`flip-20261105-dwell-prof`,
segment ~582 s drive-span, sampler perturbation +1.0 ms:
combined frame median 34.0 vs the 33.0 baseline). The walk
thread spends 68.4% of samples waiting (dominated by the
`nv2a_ack_wait` chain, 63.4% inclusive — starved for guest
pushbuffer). Split of the ACTIVE compute (30.8% of thread
samples): **STATE 39.0%, DECODE 37.0%, DATA (memcpy/memset)
17.5%, LOOKUP 4.2%, OTHER 2.3%** — lookup/caching is a
non-target (largest single lookup, `win_match`, is 1.9% of
active). Top lever-1 targets with ceilings against the
~16 ms walk: state application ~6.2 ms, decode ~5.9 ms,
data copies ~2.8 ms. Known blind spot (noted per the
commission, not fixed): the deko3d renderer thread's
internal work does not register with the sampler and is
outside this attribution.

## HALF-RATE STEP 0 — the current 30 fps signature on shipped code (evidence only, no verdicts)

Commission: capture the shipped naive throttle's behaviour
for the Phase 1 fpscap fix proof. Build: landed
v0.4.1-switch (resident NRO 211f4778…). Options: baseline
manifest with two same-length line swaps — `fps=60` → `fps=30`
(RECOMP_FPS; the throttle is `fpscap_throttle`,
src/nv2a_dk/nv2a_dk.c:3089) and RECOMP_DK_HITCH_MS 25 → 17
(`opts-half30.txt`, 5,983 B, SHA-256 c7aeb2a2…). Scene: the
garage (Stg00 — Game Data 1's load destination), 320 s idle
dwell, no input after arrival. Frame rate restores to 60
automatically (the driver restores baseline options with
read-back after the run; settings file untouched,
92e6aa90… verified in the closing CHECKS).

**Two valid captures** (both uniform stock clocks
1020/384/1331 by their own [SWPERF], both arrival-verified,
zero corrective inputs needed in either): the canonical log
`flip-20261105-half30-garage.log` (attempt C4) and the
corroborating `flip-20261105-half30-garage-void22.log`
(attempt C2 — see the misjudge note under Method). The two
agree on every figure below within noise.

Canonical (C4) numbers. Segment = the garage [AREA] line
(t=109.1, game frame 3394, flip 3184) to end of log:
333.4 s. At the 17 ms hitch threshold, 6,775 hitch intervals
(frames 3185–9959) — at this cap nearly every frame exceeds
17 ms, so the hitch population is effectively the full
interval population.

- **Interval distribution (ms : count):** 32:17, **33:514**,
  34:6, 38:1, 39:1, 42:1, 43:2, 45:3, 46:4, 47:11, 48:83,
  **49:1,761, 50:3,449, 51:866**, 52:35, plus ~15 single
  frames scattered 54–422 ms. Median **50 ms**, mean 48.8 ms.
- **Series shape:** after load transients, ~520 frames run
  at 33 ms (the true 2-vblank cadence), the series then
  slips through 47–48 ms and **locks to 49–51 ms
  (3 vblanks) for the remaining ~95% of the dwell**. In
  steady state this is not a strict long/short alternation —
  it is a one-way phase slip from the 2-field cadence to
  the 3-field cadence, early in the dwell, then stable.
- **Effective rate: 20.3 fps** at the "30 fps" setting
  (C2: 20.4 fps). **vblank:present = 2.95** (C2: 2.94) —
  computed as 60 Hz panel rate × segment seconds ÷
  presents; the build logs no `[NV2A] vblank -> ISR claimed
  it` lines in the garage segment (count 0), so the ratio
  is computed from the panel rate, not from counted ISR
  claims. A true 30 fps would read 2.00.
- **[perf] in segment:** 167 blocks; GPU timestamp-reported
  a flat ~10.3 ms/frame (median; steady blocks 10.0–10.8) —
  under the cap this window appears to include
  throttle-held time, so the interval histogram above is
  the cadence evidence, not the GPU column. Scale read
  x1.50 dynamic in sampled blocks.
- **Timed in-game event: none available.** The garage at
  idle offers no clean known-duration animation or
  cutscene to time; recorded as unavailable, per the
  commission's fallback.

Method — the attempt history, honestly (11 attempts before
the two valid captures):

- **v1 driver (×3, stashed -void1/2/3):** the standard
  60 fps-timed load chain desyncs at fps=30 — the menus
  advance at half rate and the chain fired one step ahead
  of the prompts; captures show the game left sitting at
  the "Pick a Game Data" picker (Brandon saw this live
  ~21:57 NST and flagged it). No load ever completed; no
  Stg00 data exists in these logs. void1 and void3 were
  stock-clock boots, void2 was a 307 boot — all three are
  invalid on arrival failure, not on data.
- **B series (×4, stashed -void11…14):** the arrival gate
  tried to read `Logs/last-run.log` mid-run; the
  running game holds that file locked (the file service refuses the read while the game holds it: "Device or resource busy" — earlier "live pulls" in
  this campaign's history were of dead processes). All
  four aborted by design with no dwell. B1 additionally
  lost its driver 11 s after launch to the 22:10 LOQ WSL
  instance death; its orphaned title session was closed
  by the next driver.
- **C series (pixel arrival gate):** a screen capture's
  yellow fraction separates the picker family (~68%
  yellow) from in-game (~1%) cleanly; the gate confirmed
  arrival before the dwell with no blind inputs. C1
  arrived and dwelled fully but booted at GPU 307
  (void21); C3 likewise 307 (void23). **C2 and C4 were
  valid but initially stashed as void22/void24 by a
  wrapper judging bug** — its clocks grep used an
  unquoted bracket expression that cannot match the
  literal CHECKS line, so every C attempt auto-failed the
  wrapper's judge. Both were recovered by direct
  inspection of their own drive logs (DONE + clocks seen
  [('1020', '384', '1331')] + Stg00 present + settings
  intact + baseline restored) and re-verified by hand;
  C4 was copied to the canonical run name, C2 kept under
  its stash name as the corroborating capture.

## Voided and NO-RUN attempts

- **flip-20261105-cal** — designated calibration, not a baseline
  (over-span 279.6 s, pre-trim departure; see its section above).
- **base-r3 attempt 1** — VOID, twice over. The first coordinator's
  driver died during the departure phase (its drive log ends at
  91.8 s, "load chain fired"); the orphaned session in fact reached
  Stg30 (99th Street, t=92.5) on the departure bursts already fired,
  then sat undriven in-area. The successor closed it at takeover and
  pulled the partial log as evidence
  (`flip-20261105-base-r3-attempt1-partial.log`, 1,367,001 B). The
  partial log's [SWPERF] clocks read **gpu 307** — a non-stock boot,
  so the attempt would have been VOID on the clock gate even if the
  patrol had been driven.
- **base-r3 attempt 2** — VOID, twice over. The successor's first
  batch was launched detached (nohup) and the LOQ's WSL instance
  died ~2 min in (instance uptime read 0 min on the next connection),
  killing the driver after the departure bursts with the patrol
  undriven. The successor closed the orphaned session and pulled the
  partial log (`flip-20261105-base-r3-attempt2-partial.log`,
  2,001,132 B): it reached Stg30 (t=92.4) — and its clocks also read
  **gpu 307**, so it was void on the clock gate regardless.
  Method correction recorded: all further batches run in held
  foreground sessions, never detached. Note the pattern: the
  morning's three boots (cal/r1/r2) all read 384; both boots since
  the takeover read 307. Boot probes follow to measure whether 384
  still occurs before full runs resume.
- **f4-tex1-r2 attempt 1** — VOID on the clock gate (drive log
  clocks seen: 1020/**307**/1331). Log preserved as
  `flip-20261105-f4-tex1-r2-void1.log`; attempt 2 was VALID and is
  the run reported under F4a.
- **f4-tex2-r1 (×2 attempts) and f4-tex2-r2 (×2 attempts)** —
  NO-RUN, collision aborts (successor #2 stint, ~14:01–14:04
  NST): the foreground was the SPM launcher, not HOME; the
  driver aborted on "launch failed" each time. No JSRF session
  ever started, no log pulled, nothing measured. Full account
  in the collision section above. F4c remains un-run.
- **Boot probes 1–3** (0-cycle boots, baseline options, successor):
  probe 1 gpu **307**, probe 2 gpu **384**, probe 3 gpu **307**.
  Stock boots still occur — the clock state is a per-boot lottery
  (today: 4 of 8 boots at 384). Campaign consequence: every run
  slot is attempted until its log passes the clock gate (voids
  preserved and renamed, re-run under the same slot per §3.5's
  spirit); expected cost ≈ 2 boots per valid run. Cut order
  applied in advance: F7 is cut (formality — baseline in-segment
  [gpu] count is 0), then F6, F5, F4c if the day runs short, per
  FLIP-MATRIX §3.
- **Coordinator #3 stint voids (all GPU-307 clock-gate voids,
  logs preserved under -voidN names, each narrated in its
  run's section):** F4c r1 ×1, r2 ×2; F1 r1 ×2; F6 r1 ×2;
  C2 ctrl r3 ×1, flip r1 ×1; C3 r2 ×2; S2 r3 ×1; replay F2
  r1 ×1, r2 ×2. F7 was cut as a formality (see above). The
  walk dwell was valid on its first attempt. No console
  collisions occurred during this stint — the amended
  claim + capture guard held throughout.
- **Half-rate step 0 attempts:** v1 ×3 (arrival failure at
  the Game Data picker — the 60 fps-timed chain desyncs at
  fps=30), B series ×4 (arrival gate aborted by design;
  the running game locks `Logs/last-run.log` against file-service reads while it runs), C1 and C3 (full dwells, GPU-307 boots),
  C2 and C4 (valid captures initially mis-stashed by a
  wrapper judging bug and recovered — full account in the
  HALF-RATE STEP 0 section above).
- **Infra incidents, coordinator #3 stint:** repeated LOQ
  WSL instance deaths and connection drops (notably the
  22:10 instance death that killed the half-rate B1 driver
  11 s after launch, orphaning a title session the next
  driver closed). Mitigation that held: batches attached
  to a live exec session plus a parallel WSL holder; after
  any drop, `ps aux` was checked before any console contact
  so no session was ever double-driven.

## Card final resting state

_(Campaign COMPLETE — coordinator #3 close-out, 2026-11-05
evening, all values verified by read-back over the console file service after the final run.)_

- The console claim **released and verified absent** (the claim, held since 15:19:35 -03:30 LOQ
  clock; released at close-out).
- **Resident NRO: the landed v0.4.1-switch build** —
  44,882,117 B, SHA-256
  211f4778cc65b2b02da4243eb4292acf1808c18cb17e9bedcd5c2b9967adc6d6
  (full-pull hash verified at close-out; main == tag ==
  5b0e0ec, published as Latest).
- **options.txt = the prenight file, restored** — 6,008 B,
  SHA-256 1c2a2772…, byte-match verified on read-back
  (frame rate back at its prenight value; every campaign
  driver had already restored the baseline manifest after
  its own runs).
- **settings: 246 B, SHA-256 92e6aa90…** — untouched
  throughout the campaign.
- No game running at release (`game name` probe:
  running=0); the console is free for the next claimant
  (menu build I's proof, per the parent agent's sequencing).
- Shader pack unchanged: `dk_shaders.pak` 1,417,708 B,
  mtime 2026-11-04 18:18:40 (card clock) — predates the
  campaign; every non-void campaign log, including all
  eight replay logs, reports zero runtime shader builds,
  so no A/B in this record carries the uam sched-word
  confound described in MVP-NX-DEEP-DIVE §10.

Campaign scope as completed: F4c, base-d1, F1, F5, F6,
base-d2, C1, C2, C3, S2, the replay verification pass, the
walk attribution dwell (WALK-ATTRIBUTION-2026-11-05.md),
and half-rate step 0 — all sections above. F7 was cut as a
formality (baseline in-segment [gpu] count is 0). The
upstream v0.6.8 assessment was lifted to a parallel worker
mid-campaign and is not part of this record.
