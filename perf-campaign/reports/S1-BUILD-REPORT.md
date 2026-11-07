# Campaign S-1 — value-gate vp uploads: BUILD REPORT (Nov 6, 2026)

Desk only, zero console contact, nothing pushed. Spec: STATE-STUDY.md
§3 S-1, priced by WINDOW1-REPORT.md Job 1 (prog uploads 94.0% no-op,
const 71.0%, xfctx 98.7%; combined prog+const no-op 85.9%; S-1 priced
≈0.5–0.6 ms, the largest single state item).

## Tree and commits

LOQ fork `~/jsrf-nx-s1`, branch `s1-vp-gate`, local commits only:

- `d6edf19` — campaign base (rc042 `53a83f2` + menufix). Verified
  before forking: `git diff --stat 53a83f2 d6edf19` = exactly one file
  (`src/third_party/uam/source/uam_api.cpp`, +23).
- `a5a5c2b` — "S-1: value-gate the vp upload path
  (RECOMP_DK_VP_GATE)". One file, `src/kernel/nv2a_pb_exec_dk.c`,
  +67/−17. Author Eladerino, no push.

## What the gate does, per path

Toggle `RECOMP_DK_VP_GATE` (the study's name), read once via the
tree's cached-getenv idiom: on iff set and != "0"; absent = shipped
behaviour. The gate is a *proven-no-op* skip only: a word is skipped
when the destination slot exists and already holds the word's exact
bits. Changed words take the shipped statements unchanged.

- **`vp_upload`** (single-word path via `vp_method`): a proven no-op
  skips the store, the `vp_const_dirty` mark, and the
  `prog_gen`/`const_gen`/`gen` bumps. The load counter still
  advances on `lane == 3` exactly as shipped — position semantics
  untouched. A word whose load is past the file has no slot to prove
  against and bumps exactly as shipped.
- **`vp_upload_run`** (the bulk run path — see note below): per
  word, `shadow_write` runs as always (already value-gated). Prog
  words store and count only when changed (out-of-range words count
  as changed, as shipped); `prog_gen += changed` instead of `+= kp`
  with the gate on. Const words: first/last *changed* rows bound the
  dirty range instead of first/last *written* rows, so unchanged
  edge rows no longer widen it; `const_gen += changed-words`;
  `gen += total changed`. With the gate off the three bulk adds use
  the shipped expressions (`kp`, `k - kp`, `k`) literally.
- **`vp_xfctx`** (rows 56–59: eye/fog/viewport-scale via `vp_method`,
  viewport offset c[59] via `capture_transform` — all four rows
  funnel through this one function): a proven no-op returns before
  the store, the dirty mark, and both generation bumps. This is the
  path that removes the constant range-widening of rows 56–59 on
  every frame's XFCTX re-emission (98.7% no-op in Window 1).

Compound effect in `q_draw`, as designed: a draw preceded only by
value-identical uploads now sees unmoved generations, so the
136-row program `rows_diff` scan does not run and no constant range
is shipped at all. Mixed bursts ship a narrower constant range
(interior unchanged rows still ride inside it, values identical).

Note on scope vs the study text: the study named `vp_upload` and
`vp_xfctx` from the dwell's attribution, but the bulk of the
~48K words/frame flows through `vp_upload_run`, which inlines the
stores and does its own bulk generation accounting (it never calls
`vp_upload`). The gate therefore covers all three sites; S-0's
counters were placed in all three for the same reason. The study's
flagged hazard — a consumer treating a gen bump itself as an event
— was checked: `nv2a_raw_batch_hook` is never assigned anywhere in
the tree (declared and tested only), so the defensive
`vp_const_gen_prev = 0` path is inert in this build; and the gate
compares against the slot's current bits at upload time, so any
other writer's changes are seen correctly regardless.

## Correctness gate (desk harness) — PASS

Captest-pattern harness (`s1-harness/` beside this report; sources
also in the fork): the upload path, `describe_state`'s generation
hand-off, and `q_draw`'s shipping decisions + `rows_diff` +
`runs_write` + `regs_collect` extracted **verbatim** (literal-
substring extraction) from the edited tree and from pristine
`d6edf19`; randomised streams (single words, runs crossing the
prog/const boundary, identical re-upload bursts, partial overlaps,
changed-after-identical, out-of-range load sets, XFCTX writes,
program starts) interleaved with draws. 24 seeds × 20,400 ops =
**489,600 ops per mode, 117,450 draws compared**.

- **Gate OFF = pristine, byte-identical**: the edited binary with
  the toggle unset and with `=0` produced logs byte-identical to
  the pristine binary's (all three logs 41,147,276 B, `cmp` clean)
  — same shipments, same generation counters, same shadow blocks,
  same final state, over every draw.
- **Gate ON = same downstream state, less work** (comparator over
  the per-draw logs): program runs byte-identical at every draw;
  shadow block lists and content hashes identical at every draw;
  every constant row the gate shipped carried the true current
  bytes; **every constant row the gate skipped was proven redundant**
  (the consumer already held the current value); consumer state
  identical after every one of the 117,450 draws; final `s_vp`
  contents, shadow, and both queue mirrors byte-identical per seed;
  `prog_load`/`const_load`/`prog_start` identical; the gate's
  generations never ahead of pristine's at any draw.
- **In-process invariants held in all four runs** (asserted inside
  the harness, verbatim queue code): mirror == live contents after
  every draw; a handed generation that did not move means contents
  did not move; when the constant chain is taken, every differing
  row lies inside the handed dirty range.
- **The canonical case**: the scripted full-upload → draw →
  identical-re-upload → draw phase (seed draw 1, all 24 seeds):
  with the gate on, the second draw shipped 0 program bytes,
  0 constant bytes and ran 0 `rows_diff` calls; pristine paid the
  136-row scan plus the full-range constant shipment.
- Stream totals (synthetic stream, not the patrol mix): rows_diff
  calls 62,038 vs 62,408 pristine (−370, exactly the draws whose
  scan the gate skipped: 370 × 136 = 50,320 fewer rows scanned,
  8,438,512 vs 8,488,832); constant rows shipped 1,735,457 vs
  1,805,773 (−70,316, every one proven redundant); 887 draws
  skipped a constant send pristine paid.

Honest process note: the comparator's first pass reported mass
"wrong bytes" failures — its own bug (it compared the gate's
shipments against the pristine reconstruction *before* applying
the current draw's pristine shipments, i.e. one draw stale). The
in-process invariants had already passed in all four runs at that
point; the fixed comparator, re-run on the same logs, is the PASS
above. No harness or tree code changed between the two passes.

## Build

Prescribed command, this fork's own work dir, held foreground
session, the only build on the LOQ:

```
cd ~/jsrf-nx-s1 && WORK_DIR=$HOME/jsrf-switch-build-s1 JSRF_GEN_OPT=-O3 \
  SKIP_RECOMP=1 tools/switch/build.sh "<ISO>"
```

- NRO: `~/jsrf-switch-build-s1/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
  on the LOQ — **44,910,789 B, sha256
  `e078ab0fcc9c7e162d53b74e26aae683b44364898c8902ec87c1c974ad78d152`**
  (build-reported, then independently re-hashed).
- Version string in the NRO: `0.4.1-switch-7-ga5a5c2b`;
  `RECOMP_DK_VP_GATE` string present in the binary.
- Tree re-verified intact after the build (HEAD `a5a5c2b`,
  `src/` clean).

## A/B arm spec (console, not yet run)

- **Arms**: control = this NRO with `RECOMP_DK_VP_GATE` absent from
  options.txt; gate arm = same NRO with `RECOMP_DK_VP_GATE=1`.
  Same-NRO arms isolate the toggle; the pooled 33.0 ms baseline and
  the candidate build are the cross-check.
- **Course**: 99th Street entry-plaza tower patrol, blueprint
  replay (FWD 14 s / BACK 28 s / FWD 14 s × 4), Game Data 1 load,
  shader pack frozen (`2c3a820f…`), stock-clock gate from the run's
  own [SWPERF] samples (uniformly 1020/384/1331; recycle GPU-307
  boots unmeasured).
- **Verdict read**: [perf] walk µs/draw first, frame-time buckets
  (16/33/50 ms) second, against pooled 33.0 ms. Expected size from
  Window 1's pricing ≈0.5–0.6 ms of walk time.
- **Kill gate**: replay frame-exactness — any divergence between
  the arms' replays kills the item regardless of the timing read.
  A `sample=1` dwell re-run on the gate arm is the share check if
  a movement is claimed.
