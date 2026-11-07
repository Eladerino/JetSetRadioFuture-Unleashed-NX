# Campaign console window 2 — state arms A/B (Nov 6, 2026)

Console claim held 2026-11-06T14:35-03:30 → released
~16:07 NST (deleted, absence read back). Foreground capture-verified
before first input on every boot (HOME + JSRF tile focused at claim;
HOME again at close-out). Build under test: s23 fork HEAD `25f491f`,
version `0.4.1-switch-9-g25f491f`, NRO 44,914,885 B / `7ebe5931…`
(staged via LOQ Downloads, resident byte-verified by swap-nro.py).
All runs on the frozen pack `dk_shaders.pak` 1,420,300 B /
`2c3a820f…` (hash-verified before and after; never rebuilt).
Settings `92e6aa90…` verified unchanged after every run. The save
was never written (loads only).

**Card as-found (verified at claim, matching Window 1's close-out
byte for byte):** candidate NRO `084455ca…`, options 6,055 B /
`60968312…`, settings 246 B / `92e6aa90…`, pack `2c3a820f…`. The
as-found options were snapshotted (`~/flip/w2-options-asfound.txt`
on the LOQ) and used as the base for every arm's options (arm file =
as-found + that arm's toggle lines, LF-appended, read-back verified
at install). Close-out restored the as-found options byte-exact.

**Method — one translation of the brief, stated plainly:** the
brief's correctness gate was pad-replay frame-exactness, but this
build carries no replay engine — `strings` on the s23 NRO finds no
`JSRF_PAD_REPLAY` / `PADREC` / `pad-record` (the replay exists only
in the separate recorder build, `9484e735…`). The arms therefore ran
the live tower patrol exactly as Window 1 did (step-verified manual
load chain + drive.py's departure/patrol machinery, FWD 14 /
BACK 28 / FWD 14 × 4, Game Data 1), and the kill gate ran as its
live-patrol equivalent: (a) stock-clock gate from each run's own
[SWPERF] samples, (b) segment purity (Stg30 ×1, MAP/TRACK/CUSTOM 0),
(c) segment structure identical to control's, and (d) for S-3 —
whose failure mode is a wrong picture by construction — patrol
capture equivalence against control, with the non-S-3 arms as the
timing-jitter baseline. No arm was killed by the gate; see the
capture table under S-3.

**Order run:** control → S-3 → S-2 → STATE_TRIM → S-2+S-3 → all
three (the S23 report's suggested order). One boot per arm (the
toggles are cached getenv reads at boot; no boot sharing).
Verdicts ride on the [perf] walk µs/draw first (per-block median of
walk ms/frame × 1000 ÷ draws/frame over the Stg30 segment, parsed
identically for all arms), frame buckets second, against the
within-window control first and the pooled 33.0 ms second.

---

## The load-dialog root cause (Window 1's method note, completed)

This window burned nine void boots on the save-picker YES/NO
dialog before isolating the mechanism; it revises Window 1's note
and should be the standing method for future windows.

What Window 1 established stands: the dialog opens on NO, the
D-pad is entirely inert, and the selection follows the left
stick's level (deflect left → YES renders selected; the rendering
is a red pulse on the selected option's text). Window 2 adds,
with measurements:

1. **The two-patch classifier needed recalibration.** Over ~50
   preserved dialog captures, YES-selected renders YES-patch
   R−B of 17–30 with NO-patch at ≈0; NO-selected renders NO-patch
   R−B of 23–84 with YES-patch at 0. The shipped threshold
   (R−B > 35) sat above most YES renders (Window 1's own verified
   dlg-held capture reads 30). Window 2's gate: YES ⇔ yrb ≥ 15
   and nrb ≤ 5; NO ⇔ nrb ≥ 15 and yrb ≤ 5; anything else is a
   pulse mid-phase (resample). The two signatures never overlapped
   above 15 on the wrong side in any capture.
2. **The confirm path was the real defect.** On non-latch boots a
   flick moves the selection to YES only transiently — the
   capture + classify latency (~1–2 s) lets it fall back to NO
   before a later confirm lands. Worse: confirms sent through
   `input click` or `input click-seq` **neutralise the held stick
   before firing**, so even a confirm sent while the stick was
   physically held arrived after the selection had fallen back —
   three in-hold seq confirms on one boot all landed on NO with
   YES verifiably rendered at sample time. Window 1's two
   successes were latch boots, where the fallback never happens.
3. **The reliable confirm is a raw `input press B` (0.8 s) +
   `input release B` sent while the stick is held** — no
   sequencer, nothing resets the stick. Proven interactively
   (IX2: hold samples MIXED/NO/MIXED/YES, raw press → loaded),
   after which s2s3 and all-three loaded first-try, and the trim
   arm's sixth boot was salvaged into its valid run with the same
   primitive. Flick-then-confirm remains valid on latch boots
   (control and S-3 loaded that way), but hold + raw press is
   boot-independent and is the recommended default.

Per-boot latch variance is real (control latched on a round-1
flick; S-3 on round-1 flick sample 2; the trim boots would not
latch at all), so future windows should budget dialog retries per
boot exactly as they budget GPU-307 retries.

---

## Per-arm results

All arms: clocks uniformly 1020/384/1331 in the run's own [SWPERF]
samples; purity Stg30 ×1, MAP/TRACK/CUSTOM 0; version
`0.4.1-switch-9-g25f491f`; segment scale x1.50 dynamic in every
block except STATE_TRIM (x1.25 throughout — flagged there).

| Arm | Toggles | Log | Combined 400–699 median | Walk µs/draw med (mean) | Walk ms/frame med | Draws/frame med |
|---|---|---|---|---|---|---|
| Control | — | `w2-ctrl.log` `a281671d…` | 32.0 (n=3079) | 19.672 (21.140) | 16.70 | 844 |
| S-3 | DIRTY_LIST | `w2-s3.log` `2513f332…` | 32.0 (n=2958) | 19.561 (20.977) | 16.20 | 827 |
| S-2 | CAPTURE_FAST | `w2-s2.log` `d597cdfa…` | 32.0 (n=2908) | 19.335 (20.279) | 16.00 | 823 |
| STATE_TRIM | STATE_TRIM | `w2-trim.log` `2d1a33af…` | 32.0 (n=2934) | 20.147 (21.227) | 16.70 | 819 (x1.25) |
| S-2+S-3 | both | `w2-s2s3.log` `2e75cea5…` | 32.0 (n=2935) | 19.331 (20.758) | 15.95 | 817 |
| All three | all | `w2-all.log` `be38dbae…` | 32.0 (n=2910) | 19.089 (20.441) | 15.75 | 827 |

Buckets (median ms, verbatim from the campaign extractor; every
arm): 400: 32.0, 500: 33.0, 600: 38.0, 700: 42.0, 800: 43.0,
900: 49.0, 1000: 50.0–51.0. **No arm moves any bucket.** The fit
slopes read 30.6 µs/draw for control vs 29.9–30.0 for all five
toggle arms — reported for completeness and NOT used: the delta
is uniform across arms whose block medians disagree with each
other (STATE_TRIM included), which marks it as a property of the
control boot, not five identical effects.

**Noise band, stated plainly:** one run per arm. STATE_TRIM's
solo read (+0.48 µs/draw) and the all-three read (−0.58, the
largest) cannot both be true effects of the same toggle set, so
boot-to-boot noise in the walk median is at least ±0.3–0.5
µs/draw (±0.25–0.4 ms/frame). Deltas below that band are
direction-only evidence.

### Verdicts

- **S-2 (CAPTURE_FAST) — GAIN, small.** −0.34 µs/draw median,
  −0.86 mean, −0.70 ms/frame of walk — all three reads in the
  same direction, the only arm besides the combos where that
  holds. ≈0.28 ms/frame at the segment's ~830 draws, the top of
  its 0.1–0.3 ms priced band. Sub-quantum: no bucket movement.
  This is the window's one billable gain, at single-run strength.
- **S-3 (DIRTY_LIST) — NULL within noise; correctness PASS.**
  −0.11 µs/draw median / −0.16 mean (≈0.09 ms/frame) against a
  0.1–0.25 ms price — indistinguishable from zero at this
  window's resolution. Correctness: desk harness set-equivalence
  (149,923 ops, 0 mismatches) plus on-console capture equivalence
  — mean-abs-diff vs control over the four patrol-cycle-end
  captures + end capture: **S-3 9.68, vs s2 10.77, s2s3 9.83,
  all 10.54, trim 12.55** (non-S-3 arms = timing-jitter baseline;
  S-3 sits at the low end — no picture divergence). No measured
  gain and no measured cost; keep-or-drop is a hygiene call, not
  a perf one.
- **STATE_TRIM — NO GAIN measured.** Solo read is +0.48 µs/draw
  (wrong direction), and its boot latched the dynamic scale at
  x1.25 for all 133 segment blocks (GPU median 6.9 ms vs 8.5–8.9
  elsewhere), so its frame reads are confounded as well as
  adverse; the walk figure is CPU-side and stands as read. The
  desk proof (differs from pristine in exactly the 102 dead-field
  records) is unaffected — but under the release rule (no
  measured gain, no headline) STATE_TRIM earns nothing here.
- **S-2+S-3 — GAIN ≈ S-2 alone.** −0.34 µs/draw median, −0.75
  ms/frame of walk. No additivity over S-2 is resolvable at this
  noise band, consistent with S-3's null.
- **All three — nominally largest (−0.58 µs/draw median), not
  proven better than S-2 alone.** The gap to S-2+S-3 (−0.24)
  is inside the noise band and is contradicted in sign by
  STATE_TRIM's solo read; credited as S-2's gain plus noise.
- **Frame headline:** this window's control reproduces at 32.0,
  1.0 ms under the pooled 33.0 baseline — the whole window's
  population sits there (all six arms, 32.0 combined). Verdicts
  above are therefore within-window reads; nothing in this
  window supports claiming any arm changed frame times at the
  plaza.

### Attempt accounting (voids preserved, labelled)

- Control: attempt 1 dialog void (sustained-hold-only driver;
  log `w2-ctrl-void1.log` `049c918a…`); attempt 2 GPU-307 void
  (full patrol, clocks 1020/307/1331; `w2-ctrl-void2.log`
  `5113ba82…`); attempt 3 VALID (flick latch, round 1).
- S-3: attempt 1 VALID (round-1 flick). The SSH wrapper carrying
  the run was reaped after the driver finished; the drive log on
  the LOQ is complete through DONE.
- S-2: attempt 1 GPU-307 void (`w2-s2-void1.log` `c71f7f0f…`);
  attempt 2 dialog void — a single-sample hold read confirmed NO
  (`w2-s2-void2.log` `6be26953…`, the failure that produced the
  two-sample rule, then the raw-press fix); attempt 3 VALID.
- STATE_TRIM: attempts 1–3 dialog voids (flick/hold driver
  generations 1–2; `w2-trim-void1/2/3.log` `9c6e0c68…` /
  `93a13378…` / `f88528cd…`); attempt 4 dialog void
  (five-experiment diagnostic: click-seq stick, held set-stick,
  double flick, right-stick, D-pad — none confirmed;
  `w2-trim-void4.log` `2bef539a…`); attempt 5 dialog void
  (hold-first + blind final confirm; three seq confirms landed
  on NO; `w2-trim-void5.log` `49e39bd2…`); attempt 6 VALID —
  interactive IX1/IX2 boot (A-confirm test failed, raw-press B
  while held loaded) completed as the arm run via a salvage
  patrol on the same boot.
- S-2+S-3: attempt 1 VALID (fixed driver, raw-press confirm).
- All three: attempt 1 VALID (fixed driver).
- One further infra note: a detached (nohup) launch of the S-2
  arm was reaped with its SSH session before writing anything —
  no card contact; the arm was rerun in the foreground. The
  WSL detached-session rule from the build campaign applies to
  console drivers too.

---

## Close-out verification (all read back over the console file service after restore)

| item | value | |
|---|---|---|
| resident NRO | 44,910,789 B / `084455ca46828a4416b26ccdb628720df899f29bc11299d92387b561df6234e6` (fixed candidate) | ✓ |
| options.txt | 6,055 B / `609683123e208107a041cae638d2829485a8079fbf35e2ebe3d8d66bfa2f4ea5` (as-found) | ✓ |
| settings | 246 B / `92e6aa904d60727b4d309c4eff5474cf9f8abe986e4cf81e479d5e8318d43105` | ✓ |
| dk_shaders.pak | 1,420,300 B / `2c3a820f649c57bb0bfb04cf5437af73158f1be05ae467bed81086c380a939c9` (unchanged) | ✓ |
| console | game closed, HOME menu, JSRF tile focused (capture) | ✓ |
| console claim | released, absence read back | ✓ |
| virtual-pad tool config | buttonClickSleepTime restored to 50 (controllerIdleReleaseMs was 0 on arrival; left 0) | ✓ |

The candidate was restored from its preserved copy
(`Downloads/jsrf-build-menufix.nro`, `084455ca…`) and verified by
full pull-back via swap-nro.py; the proof-window backup folder
was not touched (it holds the shipped build).

## Evidence (this folder)

- `w2-ctrl.log` (`a281671d…`), `w2-s3.log` (`2513f332…`),
  `w2-s2.log` (`d597cdfa…`), `w2-trim.log` (`2d1a33af…`),
  `w2-s2s3.log` (`2e75cea5…`), `w2-all.log` (`be38dbae…`) — the
  six valid arm runs.
- Voids: `w2-ctrl-void1.log` (`049c918a…`), `w2-ctrl-void2.log`
  (`5113ba82…`), `w2-s2-void1.log` (`c71f7f0f…`),
  `w2-s2-void2.log` (`6be26953…`), `w2-trim-void1.log`
  (`9c6e0c68…`), `w2-trim-void2.log` (`93a13378…`),
  `w2-trim-void3.log` (`f88528cd…`), `w2-trim-void4.log`
  (`2bef539a…`), `w2-trim-void5.log` (`49e39bd2…`).
- Run captures and drive logs remain on the LOQ under `~/flip/`
  (`shots/w2-*`, `logs/w2-*.drive.log`, driver `w2_run.py`
  `38515d41…` with the raw-press confirm; IX scripts `w2_ix1.py`,
  `w2_ix2.py`, salvage `w2_salvage.py`).

## Decisions handed back to the campaign

1. **S-2 is the state act's billable item**: ~0.3 ms/frame of
   walk at the plaza, sub-quantum, single-run strength. A
   second confirming run would firm it; the number to beat is
   −0.34 µs/draw against a ±0.3–0.5 noise band.
2. **S-3: correct, free, and unmeasurable here.** Ship/no-ship
   on hygiene grounds; do not bill it as a gain.
3. **STATE_TRIM: no measured gain** (solo read adverse, boot
   confounded by the x1.25 latch). Desk proof stands; exclude
   from release headlines.
4. **S-1 remains the largest unbuilt state item** (Window 1
   priced it ≈0.5–0.6 ms from the 85.9% no-op rate) — larger
   than everything this window measured, combined.
5. **Dialog method superseded**: hold the left stick and confirm
   with a raw press/release B while held; classifier thresholds
   yrb ≥ 15 / nrb ≤ 5. Window 1's method note should be read
   with this window's root-cause section from now on.
