# Campaign FPS-cap fix — build report (Nov 6, 2026)

Desk + build complete. Zero console contact; console proof is a
separate window (spec in §5). Nothing pushed.

Defect (proven in FPS-CAP-CHECK.md): the candidate's grid pacer
disables itself at fps=60 — `pace_divisor` returns 0 whenever the cap
period (16.67 ms) is at/below the emulated vblank grid period
(17 ms) — so with RECOMP_VSYNC=0 nothing gates production, light
scenes run uncapped (observed ~120), and frame-locked game speed
scales with production rate. Candidate-introduced by
halfrate-phase1 (rc042 53a83f2); shipped v0.4.1's unconditional
minimum-interval throttle does not have the hole.

## 1. Tree and commits (LOQ `~/jsrf-nx-fpsfix`, local only, Eladerino)

- `53a83f2` — rc042 (copied from `~/jsrf-nx-rc042`, clean).
- `8fec73b` — campaign base: menufix.patch applied; verified
  `git show --stat` = exactly one file
  (`src/third_party/uam/source/uam_api.cpp`, +23) before committing.
- `56ffad6` — "fps cap: restore minimum-interval hold at grid-rate
  caps (60)": one file, `src/nv2a_dk/nv2a_dk.c`, +30/−4, fpscap
  region only. Queue and swap_interval untouched; the divisor ≥2
  path is byte-for-byte unchanged (proven behaviourally in §3).

Patch file: LOQ `~/fpsfix-harness-artifacts/fpsfix.patch`
(sha256 `868ef2c475e5328d308d3acef32fd9becb682a1346372f097aa851cca6fa4de7`).

## 2. The fix

The only behavioural change is the `period_ns && !k` branch of
`fpscap_throttle`, which previously returned unheld ("at/above the
grid's rate: never engages"). Reaching that branch already proves
the case: no-cap returned earlier on `!s_frame_period`, and no-grid
fails the `period_ns &&` test and takes the deadline path below —
so `!k` here means exactly "divisor 0 because the cap is at/above
the grid rate". The branch now holds the shipped v0.4.1 discipline,
adapted to this function's structure:

- New state `s_pace_last_ns` (ns the last present released; 0 =
  none yet), reset by `frame_period_latch` alongside `s_pace_slot`
  and `s_pace_deadline_ns`.
- If `now < s_pace_last_ns + target_ns` (target = 1e9 / cap fps),
  hold via the existing `pace_hold(deadline, 0, period_ns)` — so
  the hold uses the same sliced-sleep/spin discipline and is
  accounted in `s_pace_wait_ns` ([perf] t_pace) like every other
  hold in the function.
- `s_pace_last_ns` is re-read after the hold, exactly as the
  shipped throttle re-read `s_last_present` after its sleep: the
  interval is measured release-to-release, so present spacing is
  ≥ one cap period, and queue backpressure (2-flip limit) gates
  the guest at the same rate. A scene already slower than the cap
  arrives past the interval and never waits.
- The block comment's false bullet ("never engages: the guest
  cannot complete frames faster than the clock pacing it") is
  replaced with the corrected mechanism note.

## 3. Desk gate — host harness, ALL PASS

Method: the pace block (statics, `pace_now_ns`, `pace_divisor`,
`pace_interval_want`, `pace_interval`, `frame_period_latch`,
`pace_hold`, `fpscap_throttle`) extracted **verbatim** from the
pre-fix tree (rc042) and from the patched tree, each wrapped with
the same mocked environment — virtual monotonic clock (advances
1 µs per read so `pace_hold`'s spin terminates), a 17 ms vblank
grid via mocked `kernel_vblank_grid`, `svcSleepThread` advancing
the clock, `swap_interval()` = 0, `s_swap` NULL. Simulated present
streams: each frame costs the stated production time, then calls
`fpscap_throttle(0)` with scene-level draws; release times and
added delays recorded per frame. Generator + checker preserved in
`fpsfix-harness/` beside this report; generated sources and raw
CSVs on the LOQ in `~/fpsfix-harness-artifacts/`. The run was
executed twice (the first instance's /tmp was lost to a WSL
instance restart) with identical results — the harness is fully
deterministic.

Present-interval distributions (ms; intervals between releases):

| scenario | variant | n | min | median | mean | max | max added delay |
|---|---|---:|---:|---:|---:|---:|---:|
| cap60 light (2 ms) | base | 299 | 2.000 | 2.000 | 2.000 | 2.000 | 0.000 |
| cap60 light (2 ms) | **fix** | 299 | **16.669** | **16.669** | **16.669** | **16.669** | 14.669 |
| cap60 heavy (33 ms) | base | 199 | 33.000 | 33.000 | 33.000 | 33.000 | 0.000 |
| cap60 heavy (33 ms) | **fix** | 199 | 33.002 | 33.002 | 33.002 | 33.002 | 0.002 |
| cap30 light | base / fix | 299 | 34.000 | 34.000 | 34.000 | 34.000 | 32.001 |
| cap30 heavy | base / fix | 199 | 34.000 | 34.000 | 34.000 | 34.000 | 1.001 |
| cap45 light | base / fix | 299 | 22.221 | 22.221 | 22.222 | 22.223 | 22.225 |
| cap45 heavy | base / fix | 199 | 33.001 | 33.001 | 33.001 | 33.001 | 22.225 |
| nocap light | base / fix | 299 | 2.000 | 2.000 | 2.000 | 2.000 | 0.000 |
| nocap heavy | base / fix | 199 | 33.000 | 33.000 | 33.000 | 33.000 | 0.000 |
| cap60 light, no grid | base / fix | 299 | 16.667 | 16.667 | 16.667 | 16.667 | 16.669 |

Assertions (13/13 PASS):

- Fix, cap 60 light: every present spaced ≥16.60 ms (min = median
  = max = 16.669 ms — the 16.667 ms period plus ~2 µs of harness
  clock-read quantum) ⇒ ≤60 presents/s out.
- Base, cap 60 light: 2.000 ms flat — the hole reproduced on the
  desk (release at production cost, uncapped).
- Fix, cap 60 heavy: max added delay 0.002 ms (clock-read quantum
  only — no hold ever engages); frames never delayed beyond their
  own cost.
- Fix, cap 30 light: locked at exactly 34.000 ms (2 × 17 ms grid).
- Release sequences **exactly identical** base vs fix (every frame,
  interval and delay) for: cap30 light, cap30 heavy, cap45 light,
  cap45 heavy, nocap light, nocap heavy, cap60 light no-grid. The
  divisor ≥2 path, the CUSTOM deadline path, the no-cap case, and
  the no-grid case are behaviourally untouched.

Notes on pre-existing behaviour the harness documents (identical
before and after; not this fix's business): cap45 heavy shows max
added delay 22.225 ms — the advancing-deadline path's restart-rule
arithmetic on frames slower than the cap. The no-grid cap60 case
already held pre-fix (16.667 ms) via that same deadline path,
confirming the hole existed only when the grid is present.

## 4. Build

- Serialized as required: waited until the S-1 build's processes
  had exited and its NRO had landed (14:50 LOQ) before starting;
  only one build ran at a time.
- Command (prescribed): `cd ~/jsrf-nx-fpsfix && WORK_DIR=$HOME/jsrf-switch-build-fpsfix JSRF_GEN_OPT=-O3 SKIP_RECOMP=1 tools/switch/build.sh "<ISO>"`.
- Incident, reported plainly: the SSH route dropped ~8 minutes
  into the build ("server not responding") and stayed down for
  several minutes (a WSL instance restart took /tmp with it — the
  first harness directory went with it; see §3). The build itself
  survived in its held WSL session and completed: dist timestamp
  14:58 LOQ. Post-build verification therefore re-checked the
  known WSL rollback failure mode explicitly: the source tree on
  disk still carries the fix (`s_pace_last_ns` present, HEAD
  56ffad6, status clean), and the NRO embeds the expected version
  string.
- **NRO: `~/jsrf-switch-build-fpsfix/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
  — 44,910,789 B, sha256
  `7983fd9472853f8d2f41b32114209421541ada0ed7235b39aea8ddbd44ea65b2`,
  version `0.4.1-switch-7-g56ffad6`.**

## 5. Console proof spec (for the later window — not run here)

Same claim protocol, capture-verified foreground, stock-clock
gate, and as-found options discipline as Windows 1–2. Build under
test: the §4 NRO. Reads:

1. **60-mode light scenes (the fix's target):** title → garage →
   character select, plus a light gameplay area. Pass: overlay
   never reads above 60 anywhere; skating speed normal by eye in
   the light gameplay area (Brandon's original symptom was speed,
   not just the number); [perf] **t_pace > 0** in these scenes
   (it reads 0 there on the unfixed candidate).
2. **60-mode heavy regression:** tower patrol — median frame time
   unchanged vs the pooled 33.0 ms baseline (the plaza is
   render-bound below the cap; the hold must not engage).
3. **30-mode re-check:** frame-cap row at 30 — presents still
   locked at ~34 ms on the vblank grid (the Leg 6 signature,
   99.0% at 33–34 ms), CUSTOM (n) display unchanged. This path's
   code is untouched and desk-proven identical, so this is a
   confirmation, not a search.
4. Restore Brandon's candidate build and as-found options/settings
   byte-exact at close, pack untouched, claim released.
