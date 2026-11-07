# JSRF performance campaign — findings and test data

This folder is the archived record of the performance campaign run
against the Switch port in November 2026: what was measured, what
each change was worth on real hardware, what was exonerated, and the
run logs behind every verdict. It exists so the numbers quoted in
release notes (and argued about in issues) can be checked against
the raw data.

Everything below was measured on a modchipped Switch Lite running
the port at **stock clocks — CPU 1020 / GPU 384 / memory 1331**.
That is a hard rule of the campaign, not a detail: a run counts
only if its own clock samples are uniformly 1020/384/1331. Boots
that come up with the GPU at 307 MHz happen on this console (about
one boot in three) and every such run was discarded unmeasured and
preserved as a labelled void.

## Method — the tower patrol

One fixed measurement, used for every A/B in this record:

- Load Game Data 1, travel to 99th Street, and patrol the entry
  plaza facing the big tower: forward 14 s, back 28 s, forward
  14 s, four cycles. The tower view is the heaviest part of 99th
  Street (~815–850 draws/frame), so the patrol is a worst-case
  dwell, not a tour.
- The headline frame figure is the median frame time of the
  400–699-draws frames (the "combined bucket"); the optimisation
  figure is the executor walk cost, [perf] walk µs/draw, median
  over the patrol segment.
- Verdicts ride on the walk figure first (it is finer-grained than
  the frame buckets, which quantise to the ~17 ms vsync grid),
  frame buckets second, and the pooled baseline third.
- Boot-to-boot noise in the walk median is ±0.3–0.5 µs/draw
  (±0.25–0.4 ms/frame), established in Window 2. Deltas inside
  that band are not gains.
- Correctness gates ran alongside every timing arm: stock clocks
  in the run's own samples, segment purity (the patrol area
  entered exactly once, no map jumps or track changes), and
  capture equivalence — patrol-cycle screenshots diffed against
  the control run, where a picture divergence kills the arm
  regardless of its timing.

**Pooled baseline: 33.0 ms** combined-bucket median, from five
independent baseline points (three on v0.4.0-switch, two drift
checks on v0.4.1-switch), all reading exactly 33.0.

## Verdict ledger

### Phase 1 — renderer flips and continuations (Nov 5, `reports/FLIP-RESULTS-2026-11-05.md`)

Single-option flips on the shipped build, three valid runs each
unless noted. Frame deltas are against the pooled 33.0 ms.

| Arm | Result | Verdict |
|---|---|---|
| F1 — draw nothing (anchor) | Deleting the **entire** renderer draw segment (~8.7 ms/frame of renderer CPU) buys only ~3 ms (all-hitch median 43 → 40 ms) | The renderer draw work is mostly overlapped, not pacing. The frame's centre of mass is the **executor walk**. Every draw-path lever's collective ceiling is ~3 ms. |
| F2 — alpha test off | 0.0 ms frame change (GPU timestamp-reported fell ~1.6 ms) | Exonerated as a frame lever: real GPU cost, but the GPU has headroom and does not pace the frame. |
| F3 — alpha-test proof off | 0.0 ms; dynamic resolution drops x1.50 → x1.25 | The proof is cheap correctness insurance whose measurable product is holding dynamic scale at x1.50 — resolution, not fps. |
| F4 — texture check interval (dose–response, 4 points) | Check cost priced: ≈1.0 ms/frame at every-frame, 0.50 at =2, **0.25 at the shipped default =4**, 0.13 at =8; frame medians flat at 33.0 at every point | Priced and exonerated as a frame lever — the cost is absorbed by renderer-thread idle at every setting. |
| F5 — renderer trace on | Exact null (33.0/33.0/33.0) | Trace logging overhead is certified nil on this pattern. |
| F6 — hitch log threshold 17 ms | Certified null | No 17–25 ms frame population exists on this pattern; frame times quantise to vsync multiples. |
| C1 — parked-vista GPU fence test | **Inconclusive** (pre-registered gate): fence wait 0.0 ms in all 255 parked blocks | The fence instrument structurally cannot observe GPU time when CPU ≫ GPU. Even with the documented ×1.6276 timestamp correction applied, GPU ≈ 15.9 ms at the heaviest vista — still not the pacer. All GPU figures in this record stay "timestamp-reported". |
| C2 — force GPU_MS at scale=1 | Exact null, bucket-for-bucket | GPU_MS is not a frame lever; the cost model stands. |
| C3 — resolution: scale=1 vs scale=max | Identical staircase at every bucket (33/34/39/43/44/50/51); GPU 5.5 → 8.2–8.5 ms timestamp-reported for 2.25× the pixels | **Resolution is free at 99th Street** — the frame is walk-bound, so a dynamic-resolution phase would buy nothing here. |
| S2 — runtime compiled -O3 | No frame gain (33.0 in all three runs); walk sub-read −~0.4–0.5 µs/draw | A real sub-read that does not cross a vsync quantum buys no frame time. Not a frame lever. |
| Shader-pack hygiene (all arms) | **0 in-run shader compiles in every arm of every A/B** | The persistent shader pack was frozen before the campaign; no comparison carries a shader-codegen confound. |

### The walk attribution (Nov 5, `reports/WALK-ATTRIBUTION-2026-11-05.md`)

A 500 Hz sampling dwell on the shipped build (255,424 samples,
0 lost; sampler perturbation measured at +1.0 ms): the walk thread
spends **68.4% of samples waiting** (starved for guest pushbuffer).
The split of the remaining active compute is the campaign's target
list, in the order it was worked:

| Class | Share of active walk compute |
|---|---|
| State application | **39.0%** |
| Decode | **37.0%** |
| Data movement (memcpy/memset) | **17.5%** |
| Lookup / caching | **4.2%** — a non-target |
| Other | 2.3% |

No single throat: the biggest single function is memcpy at 16.9%
of active compute, then the walk loop core (15.9%) and the
pushbuffer scan (11.4%). Window 1 re-ran the dwell with counters
on and got the same shape (state 42.5 / decode 35.2 / data 17.0 /
lookup 3.1), so the attribution is stable.

Two design studies priced every candidate fix against this split
before anything was built: `reports/STATE-STUDY.md` (the 39%) and
`reports/DECODE-DATA-STUDY.md` (decode + data movement). Combined
priced buildable total across all three acts: ≈1.6–3.1 ms of walk
time — the windows below are the measured reality against those
prices.

### Half-rate — fixing the "30 fps" mode

- **Step 0 (evidence):** the shipped "30 fps" setting is really
  **20.3 fps** — presents slip from the 2-vblank cadence and lock
  to 49–51 ms (3 vblanks) for ~95% of a dwell; vblank:present
  ratio 2.95 instead of 2.00. (`reports/FLIP-RESULTS-2026-11-05.md`,
  HALF-RATE STEP 0 section; design in `reports/HALF-RATE-INVESTIGATION.md`.)
- **Phase 1 (fix):** a pacer that advances the present release by
  a whole number of vblank-grid steps per frame (30 ⇒ every second
  vblank ⇒ ~34 ms ≈ 29.4 fps on the 58.8 Hz grid), with lag
  re-anchoring and a loading exemption. Hardware-proven in the
  release-candidate proof window: 10,649 frames, **99.0% at
  33–34 ms, zero 49–51 ms frames**. (`reports/HALF-RATE-PHASE1-REPORT.md`.)
  Phase 1 also introduced the fps-cap regression documented below;
  its fix is part of this archive.

### Window 1 — counters and the zero-copy stall (Nov 6, `reports/WINDOW1-REPORT.md`)

Runs on an instrumented counters build (S-0, `reports/S0-BUILD-REPORT.md`):

- **Vertex-program upload no-op rates:** prog 94.0%, const 71.0%,
  xfctx 98.7% of words/frame are no-ops; combined prog+const
  **85.9%** of 48,140 words/frame. The rows_diff consumer scans
  51.5 rows for every row it ships (1,407,940 scanned vs 27,360
  shipped per report window). This priced the S-1 value-gate at
  ≈0.5–0.6 ms — a price Window 3 later refuted on hardware.
- **Queue remainder split:** 1,227 KB/frame — shadow 551, fixed
  structs/headers/padding 436, const runs 233, prog runs 11.
  Shadow traffic is the largest single component, so the shadow
  branch of the data act stayed live.
- **S-3 dirty-list:** 891.5 state records/frame, dirty
  blocks/record mean 7.93, histogram mass at 3–16 blocks against a
  128-flag scan (~94% empty flags) → stays live in its 0.1–0.25 ms
  band. Verdict in Window 2.
- **S-4 run-level transform capture: NO-GO.** A REST-method census
  over 134 patrol windows found no rest method above 1% of
  ~278K words/frame; the two candidate methods never approach the
  ~0.75% scheduling gate. Not built.
- **Zero-copy reproducing boot:** the A.1 mode (below) stalls
  exactly as first observed — 9 draws, then nothing, guest alive.
  New telemetry pinned the stall: the queue's `need` sits at
  56016, **4,912 bytes past the handed-over head (51104), from the
  first block onward, and nothing ever satisfies it**. Two of the
  three candidate mechanisms were refuted by the prints
  (flip-overtaking; big-record path); the remaining placement — a
  progress store gated on an unsatisfiable need, in the
  store-delivery family — is consistent-with, with the
  uncommitted-span link explicitly unproven at this point. See
  "The zero-copy programme" below.

### Window 2 — state act A/B (Nov 6, `reports/WINDOW2-REPORT.md`)

One boot per arm on the S-2/S-3/STATE_TRIM build
(`reports/S23-BUILD-REPORT.md`); within-window control walk
19.672 µs/draw. This window's whole population read 32.0 ms
combined — 1.0 under the pooled baseline — so verdicts are
within-window reads.

| Arm | Δ walk median | Verdict |
|---|---|---|
| S-2 capture-chain pre-test | **−0.337 µs/draw** (≈0.28 ms/frame) | **GAIN — the state act's one billable item.** Top of its 0.1–0.3 ms priced band, sub-quantum (no frame bucket moves). Confirmed in Window 3. |
| S-3 dirty-list collection | −0.11 µs/draw | **NULL within noise.** Correctness PASS (capture diff 9.68 vs the 9.8–12.6 jitter baseline; desk set-equivalence 149,923 ops, 0 mismatches). Keep-or-drop is a hygiene call, never billed as a gain. |
| STATE_TRIM (dead-store removal) | +0.48 µs/draw (adverse) | **NO GAIN.** Its boot also latched dynamic scale at x1.25 throughout (confounded). Desk proof stands (differs from pristine in exactly the 102 dead-field records); excluded from release claims. |
| S-2 + S-3 | −0.34 µs/draw | ≈ S-2 alone; no resolvable additivity, consistent with S-3's null. |
| All three | −0.58 µs/draw | Nominally largest but contradicted by STATE_TRIM's solo read; credited as S-2's gain plus noise. |

### Window 3 — fps-cap fix proof, S-1, S-2 confirmation (Nov 6, `reports/WINDOW3-REPORT.md`)

- **FPS-cap fix: CONDITIONAL PASS** (the bug and fix are
  documented in their own section below). On the fixed build at
  fps=60, the overlay never read above 60 in any light scene
  across seven boots, and [perf] `t_pace` — the cap's hold time —
  reads a nonzero median of **15.90 ms** in title/menu/dialog/
  character-select segments (it read 0.0 on the unfixed candidate:
  the hold simply never engaged). Heavy patrol on the same build
  at verified stock clocks: walk 19.359 µs/draw, combined bucket
  **33.0 ms** — exactly the pooled baseline; the hold engaged in
  1 of 128 segment blocks (0.7 ms), i.e. no heavy regression.
  The 30-mode leg was **not** hardware re-proven in this window
  (see the dialog note below); it rests on the desk harness
  (cap-30 releases bit-identical between base and fix) plus the
  divisor-2 path's earlier hardware proof, and is labelled as
  such. The pass was issued conditional on an audio comparison
  only the project owner could run; he later set that check aside
  as moot ("30 doesn't work as intended yet" — a proper 30 mode
  is deferred behind the improved 60), and the fixed build is the
  resident build on his card.
- **S-1 value-gate: NULL.** Control 19.506 vs gate-on 19.468
  µs/draw — a −0.038 delta, ≈0.03 ms/frame, an order of magnitude
  under Window 1's 0.5–0.6 ms price. The desk proof stands (the
  gate skips only proven-redundant rows; consumer state identical
  after every draw — `reports/S1-BUILD-REPORT.md`), but the
  redundant stores do not cost what the census priced. Hygiene
  at most, not billable.
- **S-2 confirmation: GAIN confirmed.** Gate − control median
  delta **−0.346 µs/draw**, replicating Window 2's −0.337 almost
  exactly across two independent windows (the mean does not
  corroborate in Window 3 — the walk distribution is heavy-tailed
  — so the gain is billed on the median only, ≈0.28 ms/frame).

### Window 4 — decode pack + NEON (Nov 6, `reports/WINDOW4-REPORT.md`)

One pair per arm on the decode-pack and NEON builds
(`reports/DPACK-BUILD-REPORT.md`, `reports/NEON-BUILD-REPORT.md`).
Window condition, stated for cross-window honesty: the card's
options pinned render scale to 1, so every arm ran at a static
x1.00 (640×480) where Windows 2–3 ran dynamic x1.25/x1.50.
Within-window verdicts are exact; the control still landed in the
historical family (19.706 µs/draw, combined 33.0 ms).

| Arm | Δ walk median | ≈ ms/frame | Verdict |
|---|---|---|---|
| B.1 fence pre-scan → O(1) predicate | **−1.356 µs/draw** | **−1.10** | **GAIN, 3–4× above its 0.1–0.3 ms price.** Correctness clean (capture diffs all under the jitter band). |
| NOTE trim (unhandled-method bookkeeping) | **−1.169 µs/draw** | **−0.97** | **GAIN at its ~1.0 ms ceiling price.** Accounting contract intact: totals still tracked exactly (session totals within 0.6% of control), only the per-method ranking is off. |
| B.3 shadow skip for bulk data methods | **−0.544 µs/draw** | **−0.44** | **GAIN, just above its 0.3–0.4 ms price.** Passed the strict kill gate (any picture divergence kills it): capture diffs 5.47/5.59/5.50/1.41, no divergence. |
| A.5 free trims | +0.425 µs/draw (wrong way) | — | **NO GAIN.** Correct but not billable — hygiene at best. Its control (campaign-base build) was built at window start because the brief's assumed control did not exist; the documents won. |
| ALL-ON (B.1 + B.3 + NOTE) | **−2.663 µs/draw** | **−2.18** | **GAIN, and it cross-checks the singles:** within ~0.4 µs/draw of the singles' sum (−3.069), sub-additive as expected inside one walk. The only arm whose frame buckets move: combined 33 → **32 ms**. Elevated capture diffs visually verified as actor/animation phase drift, not corruption. |
| NEON index unpack (vs NEON off) | **−0.651 µs/draw** | **−0.54** | **GAIN inside its 0.5–1.0 ms price.** Desk harness bit-exact (64,984 cases, 0 failures). Index split, from both arms' logs: **indices are 46–58% of pushbuffer words, and 100% of indices are the 16-bit form (AE32 = 0% in every segment)** — the accelerated path covers the entire index load. |

## The fold-in set

What the campaign bought, at the standard the release notes will
be held to (measured on hardware, back-to-back, stock clocks):

| Item | Measured value |
|---|---|
| S-2 capture-chain pre-test | ≈0.28 ms/frame (two windows, median) |
| B.1 + B.3 + NOTE (as ALL-ON) | ≈2.18 ms/frame |
| NEONIDX | ≈0.54 ms/frame |
| **Total** | **≈3.0 ms/frame of executor walk** against the 33.0 ms pooled baseline |

S-3, STATE_TRIM, S-1 and the A.5 trims are correct, desk-proven
changes that measured as nulls; if they ship at all, they ship as
hygiene and are never billed as improvements.

## Dead ends and exonerations (do not re-spend here)

- **A.1 zero-copy vertex transport, as shipped: dead.** The
  read-later mode stalls after 9 draws (see the ZC programme).
  Making it work is a development item of its own, not a lever.
- **Naive state dedup: dead on arrival.** The state shadow already
  value-compares before writing; there was nothing to dedup
  (`reports/STATE-STUDY.md` §2.1).
- **Lookup/caching as a class: 4.2% of active compute.** The
  largest single lookup is under 2%. Not a target.
- **S-1 value-gate** (priced 0.5–0.6, measured ≈0.03) and the
  **A.5 trims** (measured adverse-neutral): priced ≠ real, twice.
  The census method overprices store-shaped work; both are
  recorded so the next pricing pass knows it.
- **S-4 run-level transform capture: NO-GO** on census evidence.
- **B.5 header/window merge: closed as a slot** in the decode
  study.
- **Renderer-side levers at 99th Street** (alpha test, texture
  checks, resolution, GPU_MS): collectively capped at ~3 ms by
  the F1 anchor and individually measured at 0.0. The frame is
  walk-bound; that is settled, not a mood.

## The FPS-cap bug (found by the project owner, fixed, proven)

`reports/FPS-CAP-CHECK.md` and `reports/FPSFIX-BUILD-REPORT.md`.

- **Symptom (firsthand):** with the frame rate set to 60, the
  overlay read up to ~120 in title/garage/character-select and
  light gameplay — and game speed genuinely increased with it
  (the game logic is frame-locked; this was not a counter
  artifact — the counter counts host presents, one increment per
  present, no double-count path).
- **Mechanism:** the candidate build had **no working gate at
  fps=60**. The half-rate phase-1 pacer divides the cap against
  the emulated vblank grid and returns divisor 0 whenever the cap
  period (16.67 ms) is at or below the grid period (17 ms), so the
  throttle returns unheld — its design comment states the fatal
  assumption verbatim: the guest cannot complete frames faster
  than the clock pacing it. It can: the backend queue releases
  guest completion at consume speed (the executor waits only
  beyond 2 unmade flips), and with vsync off the swapchain adds
  no wait. Cap 30 still locked (divisor 2); the hole was specific
  to caps at/above the grid rate — the shipped-default mode.
- **Origin: a candidate-introduced regression.** Shipped v0.4.1's
  throttle is an unconditional minimum present interval and holds
  every mode; the phase-1 patch replaced it. The shipped 60 mode
  never had the hole.
- **Fix:** restore the shipped minimum-interval hold for the
  divisor-0 case only (one file, `src/nv2a_dk/nv2a_dk.c`, +30/−4;
  release-to-release spacing ≥ one cap period, accounted through
  the existing pace-hold path). The divisor ≥2 path is untouched.
  Desk harness 13/13 (the base reproduces the hole — cap-60 light
  releases at 2.000 ms flat; the fix holds 16.669 ms spacing;
  cap-60 heavy adds at most 0.002 ms; cap-30/45/no-cap/no-grid
  sequences bit-identical). Console proof: Window 3, Job 1 above.
  Why the observed ceiling sat near ~120 is not derivable from
  the source tree and is recorded as an honest gap.

## The zero-copy programme

A.1 (read-later vertex transport) has a ~1.7 ms ceiling in the
decode study's pricing and is ranked **last** in its own
programme despite it: instrument → reproduce → fix → soak →
measure, never bundled with anything.

1. **Step 1 — consumer telemetry** (`reports/ZC-TELEM-REPORT.md`):
   built; Window 1's reproducing boot (above) refuted two of
   three candidate mechanisms with its prints.
2. **Step 2 — executor-side accounting** (`reports/ZC-INSTRUMENT2-REPORT.md`):
   reading the code first collapsed the question — the queue's
   `need` is written by exactly one statement (the commit path,
   touches-gated) and the handed-over head is published in exactly
   one place (the flush path, five triggers). The instrument is a
   128-entry executor event ring (gate `RECOMP_QUEUE_ZC_TELEM=2`)
   recording every commit, hand-over and gate decision in order.
   One reproducing boot with it will discriminate three exact
   shapes: the span was never handed over (fix = flush
   discipline), it was handed over but never consumed (fix = the
   consumer's walk), or the need itself is corrupt arithmetic
   (fix = the reserve/commit accounting). **Built; the boot is
   the programme's next step.** No fix has been attempted.

## Save-dialog method findings (test-rig knowledge, kept with the data)

The campaign drives the console through a virtual pad, and the
save-picker YES/NO dialog cost more void boots than everything
else combined before it was understood. Final state of knowledge
(full derivations in the window reports):

- The dialog opens on NO; its selection follows the **left
  stick's level** (deflect left → YES; release → falls back to
  NO), and the selected option's text pulses red. Selection
  classifiers: YES ⇔ YES-patch R−B ≥ 15 with NO-patch ≤ 5 (Window
  2 calibration); the presence gate is YES-patch R−B < 60 alone
  (Window 4 correction — NO-pulse peaks defeat an AND gate).
- **Scope, stated by the project owner and confirmed in
  Window 4:** on a physical controller the D-pad works fine in
  this dialog (quick left tap selects YES). Every "D-pad inert"
  finding in Windows 1–3 covers only the virtual pad's D-pad
  events **as sent through the click path**, whose commands also
  neutralise a held stick before firing — which is why
  hold-and-confirm sequences landed on NO.
- **The fix (Window 4 micro-test):** a raw press/release D-pad
  tap (not the click path) moves the selection NO→YES, and a raw
  B press confirms — first try on all 8 dialogs of Window 4.
  The rig now leads with the raw D-pad chain; the held-stick +
  same-cycle raw-press method from Window 3 is the fallback.
- **At fps=30 the dialog defeats every virtual confirm mode
  tried** (six boots in Window 3: scripted hold, interactive
  hold, sequencer moves, long flicks — the pulse crossfade
  occupies most sampled instants). Console work at cap 30 loads
  at fps=60 and switches the cap in the port menu.

## What's in this archive

- `reports/` — the design studies, build reports and window
  reports (21 documents). They are lightly redacted copies of the
  working records: console-access and build-machine operational
  details were removed or neutralised; every number, verdict,
  build hash and commit id is as recorded. Absolute build-machine
  paths were shortened to bare folder names.
- `logs/` — **123 run logs, gzipped** (139,288,286 bytes raw →
  9,799,899 bytes gz), organised by phase: `flip/` (31 — the
  Nov 5 baseline and F2/F3/F4 sets with their drive logs), `w1/`
  (7), `w2/` (15), `w3/` (43), `w4/` (21), `recorder/` (5 — the
  input-recorder blueprint and replay proof runs), `misc/` (1).
  Voids are kept and labelled as voids in their filenames; a void
  is evidence about the method or the boot lottery, and several
  verdicts cite them. Game logs are **byte-identical** to the
  originals — their sha256 values match the citations in the
  window reports exactly. Drive logs (the driver's own trace) had
  home-directory path prefixes shortened and nothing else altered.
  Window 1 and 2 drive logs and the flip-phase C1/C2/C3/S2,
  half-rate and dwell game logs were kept on the build machine
  and are not part of this archive; the reports cite them by name.
- `logs/RAW-MANIFEST.sha256` — sha256 of every log's
  **uncompressed** bytes, so any log can be verified against this
  archive and against the reports' citations
  (`zcat logs/w4/w4-b1.log.gz | sha256sum`).
- `MANIFEST.sha256` — sha256 of every file in this folder as
  published (the manifest does not list itself).

Not in this archive: screenshots and capture sets (quoted diff
values are in the reports), driver/harness sources and patch
files (the changes they describe are identified by commit id in
the build reports), and the campaign's planning documents. The
working record remains the source of those.
