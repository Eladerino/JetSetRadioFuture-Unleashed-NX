# WALK SURGERY — state application design study (the 39%)

Desk study only. No code changed, no builds, no console contact.
Commissioned as Act 1 of the renewed perf campaign ("fix where the
time goes"): the walk dwell (WALK-ATTRIBUTION-2026-11-05.md)
attributed **39.0% of the walk thread's active compute** to STATE
APPLICATION — the largest class, ~6.2 ms ceiling against the ~16 ms
walk wall (~3.9 ms of the ~10 ms sampled active time). The
queue-side study (SURGERY-DESIGN.md) explicitly left this class
untouched; Surgery 1 trimmed two bookkeeping corners of it
(NOTE_TRIM is decode-classed; STATE_TRIM covers the dead stores
inside `capture_render_state` only). This study covers the rest.

**Sources.** LOQ fork `~/jsrf-nx-rc042` @ `53a83f2` (the candidate
tree the campaign now works on), read directly:
`src/kernel/nv2a_pb_exec_dk.c` (2840 lines),
`src/kernel/nv2a_backend_queue.c` (1024), `port/switch/jsrf_switch.c`.
The state-path files are line-for-line the same size as the
5b0e0ec copies the earlier studies read, and every function cited
below matches those reads. Two baseline facts that matter:

- **Surgery 1's toggles are NOT in rc042** (grep: no NOTE_TRIM /
  STATE_TRIM in the tree). `capture_render_state` here still
  performs all 19 case stores and `note_unhandled` still does its
  per-word table work. Where this study prices a function,
  Surgery 1's already-designed trims are noted as prior art, not
  double-counted.
- Conventions (the dwell's own): a function's **ceiling** = its
  share × the ~16 ms walk wall (total elimination); **sampled** =
  share × ~10 ms sampled active compute. Estimates below are
  fractions of ceilings, stated as ranges. Nothing here is a frame
  claim: per Brandon's bar, a change counts only as a measured
  back-to-back A/B (spec in §3.0).

---

# 1. What the 39% actually is

## 1.1 The word stream it processes (measured)

The executor already censuses its words by class (`s_ws`,
exec_dk.c:1952, printed on every `[perf] pushbuffer` line). Busy
Stg30 windows from the dwell log (200–261K words/frame; the
blueprint patrol run reads 139–151K):

| Word class | Share of words | ≈ words/frame (dwell) | Taken by |
|---|---|---|---|
| Indices (AE16/AE32) | 47–56% | ~120K | bulk loops in `nv2a_pb_exec_run` |
| Programs (SET_TRANSFORM_PROGRAM) | 9–13% | ~29K | `vp_upload_run` |
| Transform constants | 6–7% | ~16K | `vp_upload_run` |
| Shadow-only state ("plain") | 14–17% | ~38K | learned fast loop |
| Inline vertices | ~0% | — | bulk |
| **The rest** | **15–18%** | **~41K** | **`nv2a_pb_exec_method`, one word at a time** |

Draws: 798–913 queue records/frame (~850 draws) in the same
windows. So state application is not one mechanism; it is the sum
of (a) per-word processing of ~120K non-index words/frame,
(b) per-draw packaging ~850×/frame, and (c) the upload machinery
for ~45K program/constant words/frame.

## 1.2 Function-by-function attribution

Named state-class functions from the dwell's hot table (share of
active compute; they sum to 35.0 of the 39.0 points — the
remaining ~4 points are a tail of sub-1% helpers the dwell did not
print: `surface_apply_clip`, `imm_attr_write`, `clear_surface`
glue, `batch_index_range`, draw-path parents).

| Function | Share | Sampled | Ceiling | What it is (file:line in rc042) |
|---|---|---|---|---|
| `nv2a_pb_exec_method` | 8.1% | 0.81 ms | 1.30 ms | The per-word dispatcher for "the rest" (~41K words/frame): semaphore/trap heads, `shadow_write`, the method switch (surface/texture/blend named cases, BEGIN/END, DRAW_ARRAYS), and the default-case capture chain (exec_dk.c:2082–2440) |
| `q_draw` | 5.2% | 0.52 ms | 0.83 ms | Per-draw record builder (queue.c:417): `regs_collect` call, prog/const generation decisions, vertex-range merging, record layout, batch-struct copies. Its data copies are attributed to `memcpy`, not here; this is its own code |
| `shadow_write` | 4.2% | 0.42 ms | 0.67 ms | The register-shadow store, called for **every** subch-0 word that is not bulk index/inline data: ~115–140K calls/frame across the plain loop, `exec_method`, and `vp_upload_run`. Already value-gated (§2.1) — this share is the call + compare + the changed-word stores |
| `vp_upload_run` | 2.6% | 0.26 ms | 0.42 ms | Run handler for program/constant uploads: per word, `shadow_write` + `vp_upload` (exec_dk.c:2446–2492) |
| `shadow_run` | 2.6% | 0.26 ms | 0.42 ms | The O(n) XOR chain that keeps the shadow word-exact over bulk data runs (exec_dk.c:2496). State-classed by the dwell but it serves the *data* methods; the queue-side study's **B.3** owns the skip proposal (gated on a reader audit). Cross-reference, not re-proposed here |
| `regs_collect` | 1.9% | 0.19 ms | 0.30 ms | Per record (~900×/frame incl. clears/flips): scans all 128 dirty-block flags in 8-byte chunks, emits the set list, clears them (queue.c:316) |
| `vp_method` | 1.6% | 0.16 ms | 0.26 ms | Per-word tail of the capture chain: the 3-entry XFCTX table scan + two range tests + switch, for every default-case word that reaches it (exec_dk.c:610) |
| `raw_batch` | 1.6% | 0.16 ms | 0.26 ms | Per-draw batch assembly parent: attribute presence, reserves, `describe_state` call, backend hand-off (exec_dk.c:1250). (Its index-rebase loop is skipped under the shipped RAW_ABS_INDICES flag) |
| `state_write` | 1.3% | 0.13 ms | 0.21 ms | Per record: header + one 64 B copy per dirty block from the live shadow + the prog/const `runs_write` calls (queue.c:382) |
| `rows_diff` | 1.3% | 0.13 ms | 0.21 ms | memcmp row scans that turn live-vs-mirror differences into runs: up to 136 program rows or 192 constant rows per invocation (queue.c:335) |
| `capture_transform` | 1.3% | 0.13 ms | 0.21 ms | Per-word capture of composite-matrix / viewport-offset / xform-mode words, incl. the `vp_xfctx` mirror into constant row 59 (exec_dk.c:2042) |
| `capture_render_state` | 1.3% | 0.13 ms | 0.21 ms | A 19-case switch (exec_dk.c:1652) that is the **first** call in the default chain — every default-case word pays its dispatch, hit or miss. Surgery 1's STATE_TRIM removes the dead *stores* inside 17 cases (a fraction of this share); the call + switch cost stands |
| `raw_direct_attrs` | 1.0% | 0.10 ms | 0.16 ms | Per-draw direct-attribute fetch setup (exec_dk.c:1107) — attribute state plumbing on the batch path |
| `describe_state` | 1.0% | 0.10 ms | 0.16 ms | Per-draw batch descriptor fill (exec_dk.c:1211): `current_surface` (a `dma_resolve`), composite + vp_offset copies (80 B), a 4-stage texture/palette resolve loop (up to 8 more `dma_resolve`), the 256 B `attr_const` copy, generation reads. Runs on every draw, changed or not |

**Reading of the table.** There is no single throat inside the
class either. The top of the pile is *dispatch and per-word
handling* (exec_method + shadow_write + the capture chain +
vp paths ≈ 20 points), the middle is *per-draw packaging*
(q_draw + raw_batch + describe_state + regs_collect + state_write
+ rows_diff ≈ 13.5 points), and `shadow_run` (~2.6) belongs to
the data path. Any single item below is worth tenths of a
millisecond; the class only moves if several land together —
the same shape as the dwell's overall verdict.

## 1.3 What the dwell cannot separate, and the instrument that can

- **Inside `exec_method`'s 8.1%**: named-case words vs the
  default capture chain vs the draw-control cases. The in-tree
  instrument for exactly this split exists: the rest census
  (`s_rest` / `rest_note`, exec_dk.c:1960–2000, gated by
  `RECOMP_PB_REST=1`, ranked output via
  `nv2a_pb_exec_rest_top`). Its header comment records a prior
  finding that disciplines this whole study: *"in JSRF's streets
  no method leads (none over 1% of the words)"* — the rest is a
  broad mix, not a hidden hot method. The counting costs ~7% of
  walk time when on (same comment), so it is a one-run diagnostic,
  never a default.
- **Changed vs unchanged uploads**: nothing in the tree counts
  how many program/constant words rewrite a slot with the value
  already there. This is the single most decision-relevant
  missing number (§2.2, §3 S-0/S-1).
- **The queue remainder split**: §2.4 — shared with the
  queue-side study's A.4; one instrumented run serves both.
- **Function-level verification of any landed item**: the walk
  sampler is verified present in rc042 — options key `sample` →
  `JSRF_SAMPLE` (port/switch/jsrf_switch.c:545), gate at
  jsrf_switch.c:1170 (`atoi > 0` starts `prof_sampler`). A
  `sample=1` dwell re-run on a surgery build is the share check,
  exactly as the original dwell was run (its +1.0 ms sampler
  perturbation is on record; shares, not absolutes, are what
  transfer).

---

# 2. The redundancy structure

## 2.1 Already gated: the shadow itself (a dead naive idea, checked)

`shadow_write` (exec_dk.c:2009–2017) compares before it stores:

```c
if (s_reg[word] == param)
    return;
s_reg[word] = param;
s_reg_dirty[SHADOW_BLOCK(word)] = 1;
```

`shadow_run` (exec_dk.c:2496–2507) is likewise exact: it XOR-folds
the run and dirties the block only if some word actually
differed. **Consequence: a guest word that re-emits a register's
current value costs one compare and does NOT re-dirty or
re-ship its block.** The producer→queue state stream is already
a true delta stream at block granularity, and the consumer
applies record blocks into a persistent shadow (`c_regs`,
queue.c:823) — there is no per-draw full-state re-application
anywhere in the walk. Any proposal of the form "stop re-sending
unchanged state" is proposing what the tree already does; the
residual costs of re-emission are the *processing* costs below,
not state traffic.

One honest wrinkle: the dirty flag is sticky within an
inter-draw window. A block whose words change and change back
between two draws still ships (with its original values). This
is bytes, not walk CPU worth chasing, and it is bounded by
genuinely-touched blocks.

## 2.2 NOT gated: program/constant generation and dirty ranges

The vertex-program upload path keeps no value discipline:

- `vp_upload` (exec_dk.c:598–612): every uploaded word is
  memcpy'd into `s_vp.prog` / `s_vp.c`, marks its row dirty
  (constants), and bumps `s_vp.gen` **and** `prog_gen` /
  `const_gen` — changed or not.
- `vp_xfctx` (exec_dk.c:571–577): same shape for the transform
  mirrors (eye c[56], fog c[57], viewport scale c[58], and
  viewport offset c[59] via `capture_transform`): store +
  `vp_const_dirty(row)` + two generation bumps per word, no
  compare.

What that buys the guest's re-emissions, per draw, in `q_draw`
(queue.c:439–469):

- **Programs**: if `prog_gen` moved since the last draw,
  `rows_diff` memcmp-scans **all 136 program rows** against the
  sent mirror and ships only differing runs. The scan is the
  cost; it is triggered by *any* program word, including a
  value-identical re-upload.
- **Constants**: if `const_gen` moved, the generation-chain fast
  path ships the dirty **range** [lo, hi) verbatim
  (`runs_write` copies the rows and updates the mirror) — the
  range is positional, not value-diffed, so unchanged rows
  inside a dirtied range are re-shipped, and rows 56–59 (the
  per-frame XFCTX constants D3D re-emits around VP draws) widen
  ranges they sit inside. The fallback path (chain broken) is a
  full 192-row `rows_diff`.

Volume, from §1.1: ~29K program + ~16K constant words/frame.
Uploads are bursty (a full program is 136×4 = 544 words; ~29K
words ≈ 53 full-program equivalents/frame), so the
`rows_diff` trigger fires per upload burst, not per draw —
worst case ~850 scans/frame is not credible; ~50–150 is.
Either way the walk pays the full per-word upload processing
(~45K × (`shadow_write` + `vp_upload`)) regardless of how much
of it changes anything, and **the changed/unchanged split of
those 45K words is unmeasured** — it is the difference between
S-1 (§3) being worth ~0.1 ms or ~0.6 ms.

## 2.3 Per-word capture chain (the default case)

A subch-0 word that is not plain and not a named case pays, in
order (exec_dk.c:2404–2440): `capture_render_state`'s 19-case
switch → `capture_transform`'s three range tests →
`vp_method`'s table scan + range tests → texture-range test →
vertex-array range tests → `imm_attr_write` → (learn-plain +
note). The state words this chain exists for (composite matrix,
viewport, XFCTX, render-state cluster 0x0300–0x03A0) are a
minority of the words that pay its full miss path. The chain is
also insensitive to repetition: the millionth identical
SET_COMPOSITE_MATRIX word pays the same dispatch as the first
(it can never become "plain" — it has side effects).

## 2.4 Per-draw packaging (~850×/frame, changed or not)

Every draw pays the full packaging path:

- `raw_batch`: presence computation, reserves, `describe_state`.
- `describe_state`: ~336 B of unconditional copies
  (composite 64 B + vp_offset 16 B + attr_const 256 B) plus up
  to 9 `dma_resolve` calls (surface + 4 stages × image/palette).
  Surgery 1 audited the two obvious trims here and rejected both
  as unprovable (attr_const consumer staleness; dma_resolve has
  no generation counter to key a cache on, and in this executor
  it is a compare, not a RAMHT walk). This study does not
  re-propose them (§4).
- `q_draw`: `regs_collect`'s 128-flag scan, the generation
  decisions of §2.2, record layout, and the batch-struct copies
  (B_HEAD + tail of `Nv2aRawBatch`) into the record.

Queue traffic bounds the state part: dwell windows show
5,473–5,906 KB/frame total, of which indices 893–949 KB and
vertices 3,464–3,697 KB; the remainder (state blocks + prog/
const runs + per-record fixed structs) is **1,116–1,260
KB/frame ≈ 1.3–1.4 KB per record** — still un-split (the
queue-side study's A.4 owns that split; §3 S-0 folds the state
counters into the same run).

## 2.5 What does NOT repeat

For calibration: vertex-program *constants that carry
per-object data* (VP-mode matrices) genuinely change per draw —
that traffic is the game's data, not redundancy. FFP composite
matrices likewise change per object. The redundancy in this
class is concentrated in (i) value-identical re-uploads and
re-emissions (§2.2, unmeasured), (ii) dispatch overhead paid
per word regardless (§2.3), and (iii) fixed per-draw packaging
(§2.4). It is not concentrated in state bytes.

---

# 3. Priced surgery items

## 3.0 The A/B every item is judged by (house standard)

Tower patrol at 99th Street (blueprint replay, entry-plaza
patrol), pooled baseline **33.0 ms** median; stock-clock gate
(the run's own `[SWPERF]` samples uniformly 1020/384/1331);
shader pack frozen. Verdict read on **frame-time buckets AND
the `[perf]` walk figure** (ms in the walk per frame ÷ draws —
both printed by the same report), plus a `sample=1` dwell
re-run whenever a share is claimed to have moved, plus
frame-exact replay as the correctness gate. One variable at a
time; every item behind its own toggle, default-off with the
toggle absent = shipped behaviour byte-for-byte, per the
tree's RECOMP_* convention.

## S-0 — Counters first (measurement; prerequisite, not a change)

- **Mechanism.** One instrumented dwell run carrying: (i) vp
  no-op counters — in `vp_upload`/`vp_xfctx`, count words whose
  param equals the target slot's current value, split
  prog/const/xfctx; (ii) `rows_diff` invocation + rows-scanned
  counters (prog/const); (iii) dirty-blocks-shipped per record
  histogram (with the queue-side A.4 byte split in the same
  run); (iv) one `RECOMP_PB_REST=1` patrol for the rest census.
  Counters behind a stats gate, in the `s_ws` style; the run's
  perturbation is acknowledged, shares are what transfer.
- **Expected gain.** None directly. It prices S-1, S-3, S-5
  and closes §2.2/§2.4's open numbers.
- **Risk.** Zero (no behaviour change).
- **Toggle.** Stats gate only; nothing ships differently.

## S-1 — Value-gate the vp upload path

- **Mechanism.** In `vp_upload` (exec_dk.c:598) and `vp_xfctx`
  (:571): compare `param` against the destination slot first.
  Unchanged ⇒ skip the store, the dirty mark, and both
  generation bumps. **Position semantics are untouched**: the
  load counters (`prog_load`/`const_load`) advance on
  `lane == 3` exactly as now, so a skipped word still advances
  the upload window. Changed words behave byte-identically to
  shipped. Effect compounds into `q_draw`: a draw preceded
  only by value-identical uploads now sees unmoved generations
  and skips the 136-row `rows_diff` scan and the constant
  range shipment entirely.
- **Expected.** Bounded above by vp_upload_run + vp_method +
  rows_diff ≈ 5.5 points ≈ 0.55 ms sampled / 0.88 ms ceiling.
  Realistic **0.2–0.6 ms**, and the honest statement is that
  S-0's no-op rate *is* the price: at a 25% no-op rate this is
  a ~0.15 ms item; at 75% it is the largest single state item
  available. Do not schedule the build before S-0 reports.
- **Risk.** Low-medium. The correctness surface is generation
  semantics: `describe_state` hands dirty ranges and gen
  values the queue chains on (`vp_const_gen_prev`). Gating
  makes "gen moved ⇔ contents moved" *more* true, which is
  the invariant the queue already assumes; the hazard is a
  consumer that treats a gen bump itself as an event (the
  raw-batch hook path zeroes `vp_const_gen_prev` defensively
  already). Desk gate: a word-stream harness (the Surgery 1
  captest pattern) comparing final `s_vp` contents and the
  *shipped row sets* gated vs ungated — shipped sets must be
  identical; raw gen counters may legitimately differ.
- **Toggle.** `RECOMP_DK_VP_GATE=1`, absent = shipped.

## S-2 — Capture-chain dispatch pre-test

- **Mechanism.** Reorder the default case's tests so the
  common miss is cheap: before calling
  `capture_render_state`, pre-test its exact method set with
  two range checks (the 0x0300–0x03A0 cluster plus the two
  strays 0x0214 / 0x1D8C). Words outside — the majority of the
  default chain's traffic — skip the call and its 19-case
  switch entirely. Optionally apply the same treatment to the
  chain as a whole (one fused dispatcher), but the pre-test
  alone is the priced item; fusion is a bigger diff for the
  same idea.
- **Expected.** A slice of the miss path inside
  capture_render_state (1.3) + capture_transform (1.3) +
  vp_method (1.6): realistic **0.1–0.3 ms**. Ceiling for the
  whole chain is 4.2 points ≈ 0.67 ms and is not reachable —
  the hits still pay full price.
- **Risk.** Low. Pure dispatch reorganization; the method set
  is enumerable from the switch itself. Desk gate: captest
  pattern over all 2048 methods × params comparing taker +
  side effects, on/off.
- **Toggle.** `RECOMP_DK_CAPTURE_FAST=1`, absent = shipped.

## S-3 — Dirty-list collection instead of the per-record scan

- **Mechanism.** Replace `regs_collect`'s 128-flag scan
  (queue.c:316, ~900×/frame) with a producer-maintained list:
  `shadow_write`/`shadow_run` already know the block at the
  moment they set its flag — when the flag was 0, append the
  block index to a small list the queue drains per record.
  The two places that invalidate list bookkeeping — the
  initial all-dirty state (exec_dk.c:303) and `q_draw`'s
  failure path (queue.c:~521, memsets all flags) — set a
  reseed flag that makes the next collect fall back to the
  shipped scan and rebuild the list. Surgery 1 called
  regs_collect "already the minimal mechanism"; that verdict
  was about its *semantics* (collect-and-clear per record).
  This item changes only how the set is enumerated, and the
  study flags the disagreement explicitly rather than
  papering over it.
- **Expected.** Most of regs_collect's 1.9 points minus list
  upkeep on changed words (one test + a rare byte store per
  *changed* word — changed words are far fewer than
  900 × 128 flag tests): realistic **0.1–0.25 ms** (sampled
  share 0.19 ms; ceiling 0.30 ms).
- **Risk.** Medium-low. Correctness surface: each block
  exactly once per record window, in a window whose boundaries
  are the collect calls (draws, clears, flips all collect).
  Consumer application is order-independent (disjoint 64 B
  regions into `c_regs`, queue.c:823), so list order may
  differ from scan order safely. The reseed paths are the
  edge cases and are enumerable. Replay frame-exactness is a
  strong gate here: any missed block is a wrong picture.
- **Toggle.** `RECOMP_DK_DIRTY_LIST=1`, absent = shipped
  (scan path retained in full).

## S-4 — Run-level transform capture (conditional on S-0)

- **Mechanism.** `nv2a_pb_exec_run` already fast-paths the
  transform-program range; add the FFP staple: an increasing
  run inside SET_COMPOSITE_MATRIX (16 words) taken as one
  call — one dispatch, per-word `shadow_write` + slot memcpy
  as now, `composite_set` set once. (Viewport-offset runs
  fold in only if S-1 has landed, since their `vp_xfctx`
  side effects interact with its gating.)
- **Expected.** **Unpriced until S-0's rest census reports.**
  The in-tree prior (§1.3) says no rest method exceeds 1% of
  words; composite words are 16 per matrix, so even a leading
  composite share caps this at roughly 0.1–0.2 ms of dispatch
  savings. Schedule only if the census shows composite +
  viewport words ≥ ~0.75% of the stream.
- **Risk.** Low-medium (run-boundary exactness vs the
  word-at-a-time path; harness + replay gate it).
- **Toggle.** `RECOMP_PB_XFORM_RUN=1`, absent = shipped.

## S-5 — Cross-reference: shadow_run skip (owned by SURGERY-DESIGN B.3)

2.6 points (~0.42 ms ceiling) of this class is `shadow_run`
over the bulk *data* methods. The priced proposal, its
s_reg-reader-audit gate, and its toggle already exist as
**B.3** in SURGERY-DESIGN.md and are not re-derived here. In
combined ordering it sits after S-1: if the reader audit
clears, it is the second-largest priced item in the class.

## Combined realistic reach

S-1 + S-2 + S-3, if S-0's numbers cooperate: **~0.4–1.1 ms**
of walk time, against the class's 6.2 ms ceiling; plus up to
~0.4 ms more if B.3's audit clears. Stated plainly: at the
33.0 ms plaza median, even the top of that range is about one
frame quantum of headroom shared with everything else in the
frame — worth taking, not worth promising a bucket change
from any single item.

---

# 4. Explicit non-targets and dead ends

- **Value-dedup of register writes / state re-shipment.**
  Already implemented (shadow_write compare,
  exec_dk.c:2013–2014; shadow_run XOR fold). Checked and
  dead — see §2.1.
- **Lookup/caching as a class** (win_match, record_tex_reg,
  find_texture/vp/surface): 4.2% total, the dwell's declared
  NON-target. `record_tex_reg` rides along with texture state
  words but belongs to that class.
- **`describe_state`'s attr_const copy and dma_resolve
  caching.** Audited and rejected as unprovable by Surgery 1
  (consumer staleness is executor-invisible; no generation
  counter exists to key a DMA cache on). Not re-proposed.
- **`capture_transform`'s contents.** Load-bearing:
  composite[16] is copied into every batch, vp_offset feeds
  c[59], xform_mode selects the batch type. Only its
  *dispatch* cost is in scope (S-2/S-4).
- **`capture_render_state`'s two live cases** (zeta_va,
  zstencil_clear) and, in this tree, Surgery 1's STATE_TRIM
  as the already-designed answer for its 17 dead stores —
  stacking S-2 on top must not double-count that fraction of
  its 1.3 points.
- **Cross-draw state references ("ship state once per
  frame").** Records are deltas already; the consumer's state
  is persistent. A reference scheme would attack bytes the
  walk barely spends (state_write is 1.3 points including its
  header work) at the price of a two-thread record-format
  change. Dead.
- **`q_clear`'s whole-render-state record copy.** Noted by
  Surgery 1, left there: single-digit clears/frame, ~72 B
  each. Still a non-target.
- **The renderer thread's state application** (pipeline
  building from `c_regs` in the DK backend): real work, wrong
  thread — the dwell's documented blind spot, outside this
  39% by construction.
- **exec_method's semaphore / trap / flip heads.** Correctness
  machinery at the top of the function; their volume at the
  patrol is fences and flips, not state, and the fence path
  has its own history (B.1 of the queue-side study). Not
  state-application waste.
- **Optimisation flags for the executor files.** Bounded by
  the campaign's S2 result (whole-runtime −O3: walk ~2.5%
  cheaper per draw, no frame movement). Not a surgery slot.

---

# 5. What was NOT verified

- The changed/unchanged split of the ~45K vp upload
  words/frame — no counter exists; S-0 is the remedy and
  S-1's price is explicitly conditional on it.
- The split of the 1,116–1,260 KB/frame queue remainder
  between shadow blocks, prog/const runs, and fixed
  per-record structs (shared with SURGERY-DESIGN's A.4).
- The method mix inside "the rest" on the *patrol* route
  specifically (the in-tree "no method over 1%" finding is
  from JSRF's streets generally; S-0's REST run confirms it
  for the tower patrol before S-4 is scheduled).
- `rows_diff` trigger frequency at the patrol (bounded
  ~50–850/frame in §2.2 from burst structure, not counted).
- Whether `exec_method`'s 8.1% splits as §1.2 assumes between
  named cases and the default chain — the sampler attributes
  to the function, not the case; the REST census bounds it
  from the word side only.
- Any timing effect of any item here. All prices are
  dwell-share arithmetic until the §3.0 A/B exists.
- SURGERY-1-REPORT.md's own tail is truncated on disk (the
  file ends mid-sentence in its desk-verification section);
  this study relies only on its intact sections plus the
  patch itself.
