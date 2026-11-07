# WINDOW 3 REPORT — JSRF perf campaign, Nov 6, 2026

Console claim taken 2026-11-06T16:22:03-03:30, released at close
(released at close, absence read back). Three jobs under one claim,
in order: (1) FPS-cap fix proof, (2) S-1 A/B, (3) S-2 confirm pair. Methods per
WINDOW1/WINDOW2 reports: blueprint tower patrol (FWD 14 / BACK 28 / FWD 14 x 4)
on the live chain, segment = first `[AREA] Stg30 (99th Street)` line to log
end, walk µs/draw = per-block median/mean of (pushbuffer walk ms x 1000 /
paired deko3d draws/frame), frame buckets from `[hitch]` lines, t_pace = the
deko3d line's "frame cap held" ms. Analysis parser validated by reproducing
WINDOW2's published numbers exactly on w2-ctrl/w2-s2/w2-all (19.672/21.140,
19.335/20.279, 19.089/20.441; combined 32.0 each) before use.

Every number below was read in a foreground-verified check (see Environment
incidents — two background deliveries this window returned plausible
completion text that direct checks refuted; nothing in this report rests on
them).

---

## JOB 1 — FPS-cap fix proof (fpsfix NRO 44,910,789 B / `7983fd94…`, version 0.4.1-switch-7-g56ffad6)

### Boot A — light scenes (fps=60)

One valid composite session (log `w3-fps-light.log`, 1,722,436 B / `cf49055e…`):
title linger -> PLUS menu -> save picker -> dialog confirmed interactively
after six void boots (see attempt accounting) -> character select -> garage ->
departure -> Stg30 entry idle. **This boot's clocks were (1020, 307, 1331)** —
a GPU-307 boot. The cap proof is a mechanism proof (the hold is time-based);
a slower GPU cannot manufacture t_pace holds, it can only make scenes cheaper.
The perf-sensitive leg (Boot B) was run at verified stock clocks.

Overlay reads (bare number, top-left), never above 60 in any capture on any of
the seven Boot A boots:

| Scene | Reads |
|---|---|
| Title / attract | 44 (attract transition), 52 (title card) |
| Picker / menu screens | 56, 60 (repeatedly) |
| Character select | 39, 59 |
| Garage intro camera | 60 |
| Garage skating bursts | 19, 22 |
| Light district (Stg30 entry, idle) | 20 |

[perf] t_pace by segment (valid Boot A log; areas Stg10 t=1.6, Stg00 t=230.0,
Stg30 t=311.6):

| Segment | Blocks | held >0.5 ms | nz median | max | GPU med | draws/fr med |
|---|---|---|---|---|---|---|
| Title/menu/dialog/charselect | 114 | 74 | **15.90 ms** | 16.00 | 1.30 ms | 31 |
| Garage (Stg00) | 41 | 29 | 4.20 ms | 14.10 | 8.80 ms | 232 |
| Light district (Stg30 entry) | 34 | 1 | 0.70 ms | 0.70 | 8.40 ms | 985 |

On the unfixed candidate these same featherweight phases read t_pace 0.0
(FPSFIX-BUILD-REPORT §2: 48/48 blocks 0.0 in the garage segment). The fix's
hold is engaging at ~15.9 ms exactly where production outruns the 16.67 ms
cap interval. Salvage from all six void Boot A logs (same build, whole-log
[perf] blocks) corroborates on every boot:

| Void log | Blocks | held == 0 | held >0.5 | nz median |
|---|---|---|---|---|
| w3-fps-light-void1 | 52 | 37 | 10 | 15.90 |
| w3-fps-light-void2 | 54 | 38 | 12 | 15.90 |
| w3-fps-light-void3 | 53 | 38 | 12 | 15.90 |
| w3-fps-light-void4 | 52 | 37 | 11 | 15.90 |
| w3-fps-light-void5 | 62 | 38 | 20 | 15.90 |
| w3-fps-light-void6 | 61 | 37 | 19 | 15.90 |

Skating speed: garage/light burst captures show normal displacement cadence
at the scenes' render rate (~20 fps in the night plaza on this 307 boot);
no runaway displacement. Corroborating only — the runaway cannot manifest in
scenes that render below the cap; the mechanism proof is the t_pace
population above.

### Boot B — heavy regression patrol (fps=60)

Valid run `w3-fps.log` (2,438,857 B / `03b8ace0…`): clocks uniformly
(1020, 384, 1331), purity Stg30 x1 / MAP 0 / TRACK 0 / CUSTOM 0, version
g56ffad6, 128 segment blocks, segment draws/frame median 817, scale x1.50
throughout.

- Walk µs/draw: **median 19.359, mean 20.313**
- Combined 400-699-draws bucket: **33.0 ms** (n=2825)
- Per-100 buckets (n, median ms): 400-499 (2144, 33.0), 500-599 (536, 34.0),
  600-699 (145, 39), 700-799 (374, 43.0), 800-899 (891, 44), 900-999 (1942, 50.0)
- t_pace in segment: 1/128 blocks nonzero (0.7 ms) — the new hold does not
  touch heavy frames.

Comparators: pooled campaign baseline combined = 33.0 (Boot B sits exactly on
it); Window 2's window population read 32.0 for all six of its arms including
its control (a documented window offset), and W2's control walk was
19.672/21.140 — Boot B's walk is inside the ±0.3-0.5 µs/draw noise band of
that control. **No heavy regression.**

### Boot C — 30-mode leg: NOT re-proven this window

Six fps=30 boots, zero loads. The save dialog's YES selection could not be
confirmed via the virtual pad at fps=30 in any mode tried: 4x w2_run
scripted dialog (voiddlg, voiddlg2, voiddlg3, voiddlg4 — hold samples sit
MIXED at 30 fps; the pulse crossfade occupies most sampled instants), 1x
interactive hold + strong-YES fast-press cycles (voidix, 7 cycles), 1x
long-flick cycles (voidflick, 14 cycles: samples read yrb 0.0, nrb up to
86 — NO never released). All voids preserved.

The 30-mode claim therefore rests on, and is labelled as: (i) the fix commit
touches only the divisor-0 branch; its desk harness shows cap-30 releases
bit-identical between base and fix (34.000 ms grid lock, 13/13 checks);
(ii) the same pacer's divisor-2 path was hardware-proven in proof-window
Leg 6 on the pre-fix RC build; (iii) Boot B's segment t_pace ~ 0 shows the
new hold does not leak into the heavy path. Not a hardware re-proof.

### Live symptom — audio distortion on the title menu (Brandon, ~17:29 NST)

Brandon, listening to the console firsthand at ~17:29 NST: "Whatever is
running right now has significant audio distortion on the title menu."
Recorded against the boot live at that minute, identified from the
drive/game log timestamps:

- **Build: Job 1's fpsfix NRO (`7983fd94…`), options fps=30** (a Boot C
  boot). It was the window's 17th boot (Boot C attempt 6, the long-flick
  phase-1 boot): launched ~17:28:15 NST, title phase ~17:28:15-17:29:40,
  dialog open at 17:29:55 (phase-1 drive log). **Clocks (1020, 384, 1331)
  — stock.** Log: `w3-fps30-voidflick.log` (1,273,763 B / `46f23f2e…`).
  No symptom was reported during the fps=60 boots (Boot A/B, earlier in
  the window).

Title-phase timing from that boot's log (the whole boot is menu-class
scenes; [perf] blocks carry no game-time, so the first ~40 blocks are
the title-phase estimate):

- Title-phase blocks: 29/40 with t_pace >0.5 ms, nonzero median
  **28.00 ms** (range 10.2-31.6); whole boot 74/85 blocks held, nonzero
  median 33.10, max 33.20. At cap 30 the pacer is actively holding
  featherweight title frames toward the ~34 ms grid slot — the title
  menu's present cadence in this boot is the pacer's held cadence, not
  a free-running one.
- Hitch frames (>25 ms): 4,426, dominated by **33 ms (2,144) and 34 ms
  (2,172)** — the 30-mode grid lock, tight. Shoulders: 32 ms (31),
  35 ms (27).
- Irregularity: 25 frames >=45 ms (max 332 ms), almost all 4-draw
  transition frames (59-179 ms spikes at boot/menu transitions), not
  steady-state cadence breaks. In the title phase proper the counters
  show a regular held cadence; nothing in the timing evidence shows
  steady-state jitter. What the counters cannot show is how the held
  ~34 ms cadence itself sounds against the unfixed build's free-running
  title menu — that is an ears-only comparison, and Brandon is the only
  audio instrument available.

**Audio control check: PENDING Brandon's listen.** Control = the
candidate build (`084455ca…`, unfixed) at the title menu under the same
conditions (the symptom boot ran fps=30). The candidate is the resident
build on the card now (restored at close-out), so the control listen is
available immediately: launch JSRF and sit at the title menu. This
instruction arrived after the window's close-out had already completed,
so the console was not left sitting in-game at the title menu; it is at
HOME with the candidate resident, game closed.

### Job 1 verdict: **CONDITIONAL PASS** (evidence strength: strong on the cap mechanism —
t_pace population across seven boots + overlay reads on every light scene;
strong on no-heavy-regression — one valid stock-clock patrol on the pooled
baseline; the 30-mode leg is desk/prior-hardware supported only, see above.)
**The pass is conditional on the audio comparison above**: visuals and
counters pass, but if the candidate's title menu is clean under the same
conditions and the fpsfix title menu is not, the fps-cap fix does not
ship as-is. That check is pending Brandon's listen.

---

## JOB 2 — S-1 A/B (s1 NRO 44,910,789 B / `e078ab0f…`, version 0.4.1-switch-7-ga5a5c2b both arms)

Toggle `RECOMP_DK_VP_GATE` (absent = control). Both arms valid: clocks
uniformly (1020, 384, 1331), purity Stg30 x1 / MAP 0 / TRACK 0 / CUSTOM 0.

| Arm | Log (sha256) | Blocks | Walk med | Walk mean | Comb 400-699 | Draws/fr med | Scales |
|---|---|---|---|---|---|---|---|
| Control | w3-s1-ctrl.log (`299b8601…`) | 131 | 19.506 | 20.450 | 33.0 (n=2829) | 824 | 1.25, 1.50 |
| Gate on | w3-s1-gate.log (`770105ec…`) | 131 | 19.468 | 20.404 | 33.0 (n=2832) | 827 | 1.50 |

Per-100 bucket medians identical between arms (33 / 34 / 39 / 43 / 44 / 50).

**Delta: -0.038 µs/draw median, -0.046 mean.** Window 1 priced S-1 at
~0.5-0.6 ms/frame from the vp no-op census (prog 94.0% / const 71.0% /
xfctx 98.7%); at this segment's ~825 draws/frame that price is ~0.65
µs/draw — an order of magnitude above the measured delta (~0.03 ms/frame).
The redundant stores the gate skips do not cost what the study priced on
real hardware.

Correctness (capture equivalence vs control, mean-abs-diff 0-255; Window 2
non-arm jitter baseline 9.83-12.55): cycle captures c0 7.69, c1 2.05,
c2 5.81, c3 9.44 — all in band. End capture 17.38 — visually verified as
the START-pad teleport flare caught mid-burst in the control capture and
faded in the gate capture; same position, scene rendered identically.

### Job 2 verdict: **NULL** (evidence strength: one valid pair, both arms
stock-clock and purity clean; correctness PASS.) S-1 is not a billable
gain. The desk harness proof (gate ON skips only proven-redundant rows,
consumer state identical) stands — the optimisation is correct, it just
doesn't buy measurable time.

---

## JOB 3 — S-2 confirm pair (s23 NRO 44,914,885 B / `7ebe5931…`, version 0.4.1-switch-9-g25f491f both arms)

Toggle `RECOMP_DK_CAPTURE_FAST=1`. Both arms valid: clocks uniformly
(1020, 384, 1331), purity clean.

| Arm (W3) | Log (sha256) | Blocks | Walk med | Walk mean | Comb 400-699 | Draws/fr med |
|---|---|---|---|---|---|---|
| Control | w3-s2-ctrl.log (`f804aba5…`) | 132 | 20.075 | 21.077 | 33.0 (n=2870) | 819.5 |
| Capture-fast | w3-s2-gate.log (`76d46cfb…`) | 131 | 19.729 | 21.163 | 33.0 (n=2829) | 812 |

Per-100 bucket medians: control 33/34/38/43/44/49; gate 33/34/39/43/44/50 —
no frame bucket moves (sub-quantum, as in W2).

**The two windows side by side (gate - control):**

| Window | Median delta | Mean delta |
|---|---|---|
| W2 (19.672->19.335 med; 21.140->20.279 mean) | **-0.337** | -0.861 |
| W3 (20.075->19.729 med; 21.077->21.163 mean) | **-0.346** | +0.086 |

The median delta replicates almost exactly across two independent windows
(-0.34 µs/draw both). The mean does not corroborate in W3 (+0.086, sign
flip) — the walk distribution is heavy-tailed and the mean is noise-led;
W2's mean agreed with its median, W3's does not. At ~815 draws/frame the
median effect is ~0.28 ms/frame of walk.

Correctness (capture equivalence vs own control): cycle captures c0 10.72,
c1 9.34, c2 10.55, c3 11.18 — in/near the 9.83-12.55 jitter baseline. End
capture 24.30 — same START-pad flare phase effect, visually verified;
scene rendered correctly.

### Job 3 verdict: **GAIN, small** (evidence strength: two-window median
replication — firmer than W2's single-run read; mean uncorroborated, so
the gain is billed on the median only, ~0.28 ms/frame, sub-quantum in
frame buckets.) Window 2's -0.34 µs/draw is confirmed, not refuted.

---

## Attempt accounting (all boots, all preserved)

**Dialog method — scope note (Brandon's firsthand correction, Nov 6):**
the physical controller's D-pad works fine for him in save select. Every
dialog finding in this report and in Windows 1-2 — in particular any
"D-pad does not move the dialog" observation behind the method — covers
only the test rig's **virtual** pad: virtual-pad D-pad events don't move
the dialog; the physical controller's D-pad does. The rest of the method
stands as established on the virtual pad: selection follows the left
stick's level, `input click`/`click-seq` neutralise a held stick, and
the confirm is a raw `input press B` while the stick is held.

21 boots total: 5 valid patrols, 1 valid light session, 15 voids.

- Boot A (fpsfix, light): voids 1-5 — dialog: held stick never moved the
  selection (void1); file-select B swallowed in picker fade-in (void2);
  two driver dialog-presence gates misdesigned in sequence (a brightness
  gate that reads the prompt bar in both states, void3; a YES-patch
  brightness gate inverted vs reality — dialog-open patches are BRIGHTER,
  void4; working gate: yrb < 60 AND nrb < 60); single-sample YES reads
  that were pulse flares, confirms landing NO (void5). Void6: two-sample
  rule held, but the selection oscillated and never held YES twice in a
  row. Raw-patch probing then showed the classifier's isolated YES reads
  under a sustained hold are pulse-cycle flares (nrb oscillating 17-32 =
  NO selected and pulsing; yrb ~0 with sporadic blips); strong sustained
  YES (yrb 35.7/20.5, two consecutive samples) appears only briefly after
  fresh deflections. The load was finally forced interactively: fire a
  raw press B inside the same sample cycle as a strong YES (yrb >= 20,
  nrb <= 5) — loaded on cycle 3. All six void logs + drive logs preserved
  (w3-fps-light-void{1..6}).
- Boot B (fpsfix, patrol): attempt 1 = full patrol at GPU 307, clocks
  (1020, 307, 1331) — void for perf (w3-fps-void307, log + drive log
  preserved); attempt 2 = killed mid-run by the LOQ WSL instance restart
  (its files rolled back with the instance; nothing preserved — the run's
  drive log never survived); attempt 3 = dialog void (w3-fps-voiddlg);
  attempt 4 = VALID.
- Boot C (fpsfix, fps=30): six voids, no valid run — see Job 1 Boot C.
  w3-fps30-voiddlg{,2,3,4}, w3-fps30-voidix, w3-fps30-voidflick (+ phase1
  drive logs w3-fps30-phase1{,b}).
- Job 2: control and gate both valid on first boots.
- Job 3: control valid (dialog needed attempts a0-a2 inside the boot);
  gate valid on first boot.

## Environment incidents (method cost, stated plainly)

1. **Two background-exec deliveries arrived out of order and described
   states that contradicted the probes I had just run** (once reporting
   a finished Boot B with a pulled log and CHECKS lines while my latest
   probe still showed the driver mid-patrol). Ground truth was
   re-established both times by short foreground probes (process list,
   absolute-path file reads, drive-log tails). (Post-window, the raw
   output of the first of those deliveries surfaced and matched the
   first Boot B run's verified end state exactly — it was a delayed
   execution describing a real completion, delivered out of order, not
   invented text. The handling was the same either way: nothing was
   taken on trust from a delivery.) After the second incident all
   checks ran as short foreground calls, drivers were launched
   detached (nohup), and a nonce file in ~/flip was read on every poll
   to detect state rollbacks. Every number in this report traces to a
   foreground-verified read or a hash-checked local copy.
2. **LOQ WSL instance restarted mid-window** (uptime 11 min when
   detected) and its journal rolled ~/flip back — the Boot B retry's
   process and files vanished. This is the documented LOQ WSL storage
   instability (AGENTS.md). The nonce protocol above was the response.
3. This VM's service restarted once, wiping /tmp scratch (SSH helper,
   parser, drivers). Helpers were recreated from ~/TOOLS.md; the parser
   was re-validated against Window 2's published numbers before reuse.
4. The console battery was never a constraint: 43% at claim, charging
   throughout, 95% at close.

## Close-out verification

Snapshot at claim matched the expected as-found state byte-exact
(candidate NRO `084455ca…`, options 6,055 B / `60968312…` — the
candidate-grown file with its appended POST lines, NOT the prenight
file — settings `92e6aa90…`, pack `2c3a820f…`). Restore used LOQ
Downloads/jsrf-build-menufix.nro (never the proof-window backup, which
holds the shipped build).

| Item | As found | At close | Verified |
|---|---|---|---|
| Resident NRO | `084455ca…` (candidate) | `084455ca…` | PUT + pull-back hash (RESIDENT VERIFIED) |
| options.txt | 6,055 B / `60968312…` | 6,055 B / `60968312…` | restore read-back match=True |
| settings | 246 B / `92e6aa90…` | 246 B / `92e6aa90…` | hash at close |
| dk_shaders.pak | 1,420,300 B / `2c3a820f…` | 1,420,300 B / `2c3a820f…` | hash at close (frozen all window) |
| buttonClickSleepTime | 50 (set to 150 for the window) | 50 | config get read-back |
| controllerIdleReleaseMs | 0 | 0 | config get read-back (left as found) |
| Game state | HOME, no game running | HOME, no game running | `game name` errors; final capture = HOME, JSRF tile focused |
| Battery | 43%, charging | 95%, charging | final capture |
| Claim | held at start | released | absence read back |

The save was never written (loads only; the picker YES path reads Game
Data 1 — no save operation exists in any driver used).

## Evidence index (this folder)

- Valid game logs: w3-fps-light.log (Boot A), w3-fps.log (Boot B),
  w3-s1-ctrl.log, w3-s1-gate.log, w3-s2-ctrl.log, w3-s2-gate.log — each
  hash-cited in its job section; matching .drive.log beside each.
- Voids, labelled: w3-fps-light-void{1..6}.{log,drive.log},
  w3-fps-void307.{log,drive.log}, w3-fps-voiddlg.{log,drive.log},
  w3-fps30-voiddlg{,2,3,4}.{log,drive.log}, w3-fps30-voidix.{log,drive.log},
  w3-fps30-voidflick.{log,drive.log}, w3-fps30-phase1{,b}.drive.log,
  w3-todialog.drive.log (Boot A phase 1).
- Captures remain on the LOQ under ~/flip/shots/w3-*/ (cycle captures for
  both A/B pairs were pulled and diffed locally during the window; the
  MAD values are quoted in the job sections).

## Notes for the parent

- **Job 1's verdict is CONDITIONAL on an audio A/B only Brandon can
  run** (see the live-symptom section): Brandon reported significant
  audio distortion on the title menu at ~17:29 NST during a fpsfix
  fps=30 boot. Control = the candidate (`084455ca…`, unfixed) title
  menu under the same conditions; the candidate is resident on the
  card, so the listen is available to him immediately. If the
  candidate is clean and the fpsfix build is not, the fps-cap fix does
  not ship as-is. Jobs 2/3 reads are patrol perf and unaffected.
- The S-1 desk harness and the S-1 hardware read now disagree: the gate
  is proven correct but buys ~0.03 ms/frame, not the priced 0.5-0.6.
  If the release notes were ever to mention S-1, it is hygiene, not a
  gain. S-2's ~0.28 ms/frame (median, two windows) is the state act's
  one billable item, unchanged from Window 2's billing.
- The fps=30 dialog behaviour is a real method finding: at cap 30 the
  save dialog's stick-follow/pulse defeats every virtual-pad confirm
  mode tried (scripted hold, interactive hold, seq move, long flicks).
  Any future fps=30 console work should plan to load at fps=60 and
  switch the cap in the port menu, or budget many boots.
- A shared build-lock protocol for the build PC was adopted mid-window.
This window ran no builds and pushed nothing, so no actions were taken
under it; flagged here for the record.
- The release stays HELD; the candidate on the card is byte-identical
  to its as-found state.
