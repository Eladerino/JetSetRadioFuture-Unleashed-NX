# Window 4 — decode pack (DPACK) + NEON index A/B

**Date:** Fri Nov 6, 2026 (claim 20:13:54 −03:30 → released ~21:40 NST)
**Console claim:** held for the window, capture-verified HOME foreground before first input; released at close with absence read back.
**Method:** tower patrol, 99th Street entry plaza facing the big tower, Game Data 1, FWD 14 s / BACK 28 s / FWD 14 s × 4, driven live. Verdicts ride on [perf] walk µs/draw median vs the within-window control (noise band ±0.3–0.5 µs/draw), frame buckets second, pooled 33.0 ms anchor third. Every arm is a **single pair** (one control, one treated boot) unless corroborated — stated per verdict.

## Window condition — read this before comparing across windows

Brandon's as-found options.txt pins **scale=1** (fps=60, log=0, scale=1). All 131 segment blocks in every W4 arm ran at a static **x1.00 (640×480)** — no dynamic scaling anywhere in the window. W2/W3 ran scale=auto (x1.25/1.50 dynamic). Within-window comparisons are exact; cross-window and pooled comparisons carry this caveat. The W4 control's walk (19.706 µs/draw) and combined bucket (33.0 ms) still land in the historical family, so the patrol itself was the same work.

## Builds

| Build | Version | NRO sha256 (head) | Role |
|---|---|---|---|
| DPACK fork `~/jsrf-nx-dpack` @ 8d93d0c | 0.4.1-switch-10-g8d93d0c | `aff65eae…` | arms 1–4, 6 (toggles boot-read from options) |
| Campaign base @ 99c15fb (worktree `~/jsrf-nx-dpack-base`, built at window start under the build PC's build lock) | 0.4.1-switch-6-g99c15fb | `70409d04…` | arm 5 (A.5-pack's control) |
| NEON fork `~/jsrf-nx-neon` @ d7ddbde | 0.4.1-switch-8-gd7ddbde | `0968e372…` | arms 7–8 |

Arm toggles (options.txt, appended to the as-found file with log flipped 0→1): B.1 `RECOMP_PB_FENCESCAN=1`, B.3 `RECOMP_PB_DATASHADOW=1`, NOTE `RECOMP_DK_NOTE_TRIM=1`, ALL-ON = all three, NEON ON `RECOMP_PB_NEONIDX=1`. A.5's trims are compiled into the DPACK build with no toggle, so A.5-pack is read as DPACK-control vs the 99c15fb BASE build (DPACK-BUILD-REPORT §5/§6 definition).

## Results

All arms: clocks uniform 1020/384/1331 in their own [SWPERF] samples; purity Stg30 ×1 / MAP 0 / TRACK 0 / CUSTOM 0; segment = [perf] blocks after the [AREA] Stg30 line.

| Arm | Log (sha256 head) | Blocks | Walk µs/draw med / mean | Draws/fr med | Combined 400–699 (n, med ms) | Δ walk med vs control |
|---|---|---|---|---|---|---|
| DPACK control | w4-dctrl.log `fb3c533a…` | 131 | 19.706 / 20.801 | 815 | 2847, 33 | — |
| B.1 FENCESCAN | w4-b1.log `85e2a30c…` | 131 | 18.350 / 19.275 | 813 | 2839, 33 | **−1.356** |
| B.3 DATASHADOW | w4-b3.log `cd5c4ee4…` | 131 | 19.162 / 20.078 | 815 | 2847, 33 | **−0.544** |
| NOTE trim | w4-note.log `2aac642a…` | 131 | 18.537 / 19.470 | 829 | 2862, 33 | **−1.169** |
| BASE (99c15fb) | w4-base.log `aae5b8e4…` | 131 | 19.281 / 20.223 | 825 | 2859, 33 | A.5 read below |
| ALL-ON | w4-allon.log `8d0ac799…` | 131 | 17.043 / 17.989 | 817 | 2919, **32** | **−2.663** |
| NEON OFF | w4-noff.log `00087efc…` | 130 | 19.224 / 20.125 | 821.5 | 2831, 33 | — (NEON control) |
| NEON ON | w4-non.log `b4b07d3b…` | 132 | 18.573 / 19.452 | 823.5 | 2853, 33 | **−0.651** vs NEON OFF |

Per-100-draw hitch buckets were near-identical across arms (400–499 ≈ [~2160, 33], 500–599 ≈ [536, 34], 600–699 = [145, 39] in almost every arm); ALL-ON is the only arm whose buckets move (400–499 median 32, combined 32).

## Verdicts

- **B.1 FENCESCAN — GAIN, well above price.** −1.356 µs/draw median (−1.526 mean) ≈ **−1.10 ms/frame** of walk, against a study price of 0.1–0.3 ms. 3–4× the top of its price and far outside the noise band. Correctness: cycle MADs vs control 7.34 / 1.88 / 1.82 / 5.66 (band 9.8–12.6 — all under); end capture visually verified (same scene, teleport flare at a different phase). Single-pair strength, corroborated by ALL-ON (below).
- **B.3 DATASHADOW — GAIN, correctness clean under the strict gate.** −0.544 / −0.723 µs/draw ≈ **−0.44 ms/frame**, just above its 0.3–0.4 ms price. The kill gate (any picture divergence kills it regardless of timing) did **not** trip: cycle MADs 5.47 / 5.59 / 5.50 / 1.41, end capture visually verified (flare phase only, no divergence).
- **NOTE trim — GAIN at its ceiling price; accounting contract intact.** −1.169 / −1.331 µs/draw ≈ **−0.97 ms/frame** vs a ~1.0 ms ceiling price. Gate: the trim's own log line is present ("per-method unhandled ranking off (RECOMP_DK_NOTE_TRIM=1): the total above is exact"), the distinct count is frozen at 0 exactly as designed, and the totals are still tracked exactly — session-cumulative last total 296,677,356 vs control's 298,569,817 (0.6% apart, consistent with frame-count variance across live runs; the desk harness's exact-equality gate applies to identical replayed inputs, which live patrols are not). Nothing was eaten: a suppressing trim would collapse the totals, not shade them by 0.6%. Cycle MADs 5.29 / 5.37 / 7.26 / 5.95.
- **A.5 pack — NO GAIN.** DPACK control (A.5 trims compiled in) 19.706 vs BASE 19.281 = **+0.425 µs/draw median (+0.578 mean) the wrong way** — the trims buy nothing measurable on hardware and may cost a hair (~+0.35 ms/frame). Null verdict, single pair. Correct but not billable — same class as W3's S-1: hygiene at best, not a perf item.
- **ALL-ON — GAIN, and it cross-checks the singles.** −2.663 / −2.812 µs/draw ≈ **−2.18 ms/frame** vs control. Sum of the three single-arm medians is −3.069; ALL-ON lands within ~0.4 of it — sub-additive, as expected for savings inside the same walk, and strong internal evidence that the three single-arm gains are real signal rather than drift. Correctness: cycle MADs elevated (10.22 / 11.97 / 14.10 / 11.91) — visually verified on the worst (c2): same scene and geometry, differences are moving actors and animation phase (ALL-ON's different effective pace through the heavy band shifts where NPCs are at capture time). No garbling, no missing geometry. **PASS.**
- **NEON index — GAIN inside its price; the split is as favourable as it gets.** −0.651 / −0.673 µs/draw vs NEON OFF ≈ **−0.54 ms/frame**, inside the 0.5–1.0 ms price. **Index split, from both NEON arms' [perf] pushbuffer lines: indices are 46–58% of pushbuffer words on patrol segments, and AE16 is 100% of the indices slice (AE32 = 0% in every segment line of both arms)** — the live stream is entirely 16-bit indices, so NEONIDX's accelerated path covers the whole index load. The two arms' split distributions agree line-for-line in shape (46% ×45–47, 58% ×39–40, 48% ×13–14). Correctness (NEON's hardware gate): cycle MADs vs NEON OFF 5.65 / 6.07 / 9.03 / 8.36, end capture visually verified (flare phase only) — consistent with the desk harness's bit-exact result (64,984 cases).

**Fold-in candidates for the next release floor:** B.1, B.3, NOTE, NEONIDX — together worth ≈ 2.2 ms/frame of walk on the tower patrol (ALL-ON measured, DPACK side) plus ≈ 0.54 ms/frame (NEON, measured on its own build). A.5's trims are not a perf reason to take anything.

## Micro-test (brief item 0) — ANSWERED YES

Folded into arm 1's save dialog, before the proven confirm: baseline classifier NO (yrb 0.0 / nrb 83.3) → **one raw DLEFT press/release (0.2 s hold, raw press/release primitives — not `input click`) → classifier YES (yrb 79.2 / nrb 0.1)** → one raw `press B` with no stick held → file loaded. The raw D-pad chain then worked **first try on every subsequent arm of the window** (8/8 dialogs). This supersedes the W1/W2 finding for the raw primitives specifically: click-path D-pad events never registered, but raw press/release events do. The driver now tries the D-pad chain first, with the strong-YES held-stick method kept as fallback (never needed after arm 1).

## Method correction found in-window: the dialog presence gate

W3's presence gate (yrb < 60 **AND** nrb < 60) is wrong at its edges. Arm 1 attempt 1 aborted because the gate never tripped while the dialog was verifiably open: at a NO-pulse peak the NO patch read **nrb 68** (capture `05-prompt0.jpg`, yrb 0.0), and two B presses confirmed NO and bounced back to the picker while the driver waited for a gate that could not fire. The YES patch is the true separator (0.0 open vs ~238 closed on every capture this window). Gate corrected to **yrb < 60 alone**; no further dialog failures in 10 subsequent boots. Attempt 1 is preserved as a void (below).

## Attempt accounting — 11 boots: 8 valid, 3 voids

| Void | Cause | Evidence |
|---|---|---|
| w4-dctrl-void1 (507,903 B, `ba265159…`) | Dialog presence-gate failure (above); run aborted at the picker | game log + drive log preserved |
| w4-allon-void1 (2,443,755 B, `a178e6cb…`) | GPU-307 boot (clocks 1020/**307**/1331) — full patrol run, discarded unmeasured per the clock rule | game log preserved |
| w4-non-void1 (2,462,380 B, `4148a6de…`) | GPU-307 boot (1020/**307**/1331) | game log preserved |

All other boots were one-shot: dialog via the raw D-pad chain, patrol, clean close. NEON OFF's boot needed 8 PLUS attempts to open the port menu (noted, no consequence).

## Brief vs documents — disagreements on record

1. **A.5-pack's control did not exist.** The brief listed A.5-pack as an arm of the DPACK NRO; DPACK-BUILD-REPORT §5/§6 defines the A.5 read against the campaign-base build of commit `99c15fb`, which had never been built. Documents win: the base was built at window start under the build PC's build lock (noted in the shared build log with its hash, `70409d04…`) and run as arm 5.
2. **As-found options.txt.** The brief expected 6,054 B / `07012c00…` with fps=30. The actual card file was **6,093 B / `5db84c89…`** (fps=60, log=0, scale=1 — Brandon had played since the brief was written). The actual file was snapshotted before any arm and restored byte-exact at close.

## Incident note (tooling, not console)

Mid-window, this VM's service restarted and wiped `/tmp`, taking the SSH helper and the log parser with it; one scp of the ALL-ON log was cut mid-transfer (detected by hash mismatch, re-pulled clean). The parser was rebuilt from the log format and **re-validated exactly** before further use: it reproduces the published W2 control (19.672/21.140, comb [3079, 32]), W3 S-1 control (19.506/20.450, comb [2829, 33]), and all six W4 arms that had been parsed before the restart, digit for digit. No arm had to be re-run.

## Close-out verification

| Item | As-found | Restored / final | Verified |
|---|---|---|---|
| Resident NRO | fpsfix `7983fd94…` (44,910,789 B) | `7983fd94…` | swap tool pull-back hash, RESIDENT VERIFIED |
| options.txt | 6,093 B, `5db84c89…` | identical bytes | pull-back hash match |
| settings | 246 B, `92e6aa90…` | unchanged | hash read at close (and after every arm) |
| Shader pack `dk_shaders.pak` | 1,420,300 B, `2c3a820f…` | unchanged | hash read at close |
| buttonClickSleepTime | 150 during window | **50** | config get read-back |
| controllerIdleReleaseMs | 0 | 0 | config get read-back |
| Console state | HOME, JSRF tile focused | HOME, JSRF tile focused, no game running | final capture viewed (21:37 NST, battery 100%) |
| Claim | held for the window | deleted | absence read-back |

All logs (8 valid + 3 voids + drive logs) are preserved in this folder alongside this report.
