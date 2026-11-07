# WALK SURGERY — decode + data-movement design study (Acts 2 and 3)

Desk study only. No code changed, no builds, no console contact.
Commissioned as Acts 2 (DECODE, 37.0% of walk-dwell active compute,
~5.9 ms ceiling) and 3 (DATA MOVEMENT, 17.5%, ~2.8 ms; `memcpy` the
single biggest function at 16.9%) of the renewed perf campaign.
Act 1 (state application, 39.0%) is Surgery 1, already desk-complete
elsewhere; overlaps with it are flagged, not poached.

Sources: LOQ fork `~/jsrf-nx-rc042` @ 53a83f2. The three files this
study lives in — `src/kernel/nv2a_pb_exec_dk.c`,
`src/kernel/nv2a_pb_scan.c`, `src/kernel/nv2a_backend_queue.c` — are
**byte-identical to the 5b0e0ec tree the first study read** (verified:
`git diff 5b0e0ec..HEAD --stat -- src/kernel/` touches only
kernel_bridge.c, kernel_file.c, nv2a_backend.h), so the first study's
line numbers hold; where they are re-cited they were re-read here.
Pricing convention is the dwell's own: share of active compute × the
~16 ms [perf] walk. Ceilings are total-elimination upper bounds;
"realistic" figures are this study's honest reach estimates.
Brandon's bar stands: nothing here is a result — every item carries
the patrol A/B that would make it one (pooled baseline 33.0 ms,
frame buckets + [perf] walk µs/draw, stock-clock gate per run,
shader pack frozen, frame-exact replay where correctness is claimed).

---

# 0. Corrections to the first study (they change prices)

1. **The fence pre-scan does not cover index runs.** scan.c has two
   bulk paths. Index runs (AE16/AE32) take the dedicated path at
   scan.c:634–664 straight to `nv2a_pb_exec_indices` — no pre-scan
   (a fixed-method run cannot contain a fence). The pre-scan loop
   (scan.c:688–692) guards only the DEKO3D bulk path for *other*
   commands. B.1's population is therefore the non-index bulk words
   (~35–45% of heavy-window words), not "every bulk word". Repriced
   in §1.3.
2. **Index words dominate heavy windows — measured now, from the
   Leg 1 control log's [perf] pushbuffer lines:** indices are
   61–67% of ~130,000–200,000 words/frame in Stg30 blocks
   (~85–125K index words/frame), transform programs 8–13%,
   constants 10–13%, shadow-only state ~8%, inline ~0%, rest ~10%.
   The AE16-vs-AE32 *word* split is still not separable from
   existing counters (`s_ws.idx` counts both; the index-count side
   is polluted by DRAW_ARRAYS expansion, which mints indices with
   no words at all), but the dwell's sampler hot line sits in the
   AE16 loop, and AE32's loop is a plain copy — AE16-first stands
   on evidence, and B.2's card now includes the two-line counter
   split that settles it (§1.4).
3. **`note_unhandled` (~5.2% self, ~6.4% with its friends) is
   classed DECODE in the dwell but is already owned** by Surgery
   1's RECOMP_DK_NOTE_TRIM (desk-complete). It is not available
   upside in this act and is not re-priced below.
4. **The first study's INLINE_VERTEX lifetime caveat is closed**
   (§2.4): nothing on the shipped path references snapshot memory
   past the walk.

---

# 1. DECODE

## 1.1 The s_reg reader audit — COMPLETE (B.3's gate)

`shadow_run` (exec_dk.c:2496–2509) maintains, for each bulk run,
two things: the final value of one shadow word (`s_reg[word] =
p[n-1]`) and the word's 64-byte block's dirty flag (set iff any word
differed from its predecessor in the chain starting at the old
value — exactly per-word `shadow_write` semantics, which the
executor's slow path applies to every subchannel-0 word in
`nv2a_pb_exec_method`'s prologue). The three data methods and their
words (nv2a_regs.h:1171–1176; word = method/4; block = word/16):

| Method | Word | Block | Other methods in the block |
|---|---|---|---|
| ARRAY_ELEMENT16 0x1800 | 0x600 | 0x60 | AE32 0x602, DRAW_ARRAYS 0x604, INLINE_ARRAY 0x606 |
| ARRAY_ELEMENT32 0x1808 | 0x602 | 0x60 | (same block) |
| INLINE_ARRAY 0x1818 | 0x606 | 0x60 | (same block) |

**Every consumer of the shadow, enumerated** (grep-complete over
src/, mesa-imported excluded — `s_reg` exists only in exec_dk.c,
plus the queue's mirror below):

- **exec_dk.c itself.** All `s_reg[...]` reads in the file:
  shadow_write/shadow_run (the maintenance itself) and
  describe_state's texture reads at exec_dk.c:1232–1233 (words in
  the 0x1B00 block). `describe_state` (exec_dk.c:1211) also hands
  the whole array out as `rb->regs = s_reg` (see queue/backend
  below). **No read of 0x600, 0x602 or 0x606.**
- **capture_render_state / capture_transform** (exec_dk.c:1652,
  2042): intercept the method *stream* (method, param) into
  s_gpu state; they never read s_reg. Their method sets are
  0x02xx–0x03xx render state, 0x0214, 0x1D8C, transform ranges —
  **no 0x18xx method is captured.**
- **The queue** (nv2a_backend_queue.c): the only caller of
  `nv2a_pb_reg_shadow()` is state_write (queue.c:385), which copies
  *dirty blocks* (64 B each) verbatim into records; regs_collect
  (queue.c:319) is a pure dirty-flag drain — it never compares
  values. The consumer thread applies record blocks into its own
  mirror `c_regs` (queue.c:823) and hands the backend that
  (queue.c:805 `bb.regs = c_regs`). So block 0x60's words reach the
  backend only as part of the 64 B block, and only when dirtied.
- **The DK backend** (nv2a_dk.c; `s_regs` = the queue's mirror,
  assigned at :5217 from `b->regs`): every read in the file —
  :4663 viewport words 0x1D98/4, 0x1D9C/4; :4679 and zmax_of
  (:4099) surface-format word 0x208/4; bind_stage (:2741) texture
  block 0x1B00 + i·0x40 words and 0x1E70/4. **No read of any
  0x1800-block word, through any alias.**
- **Diagnostics and hooks.** `nv2a_pb_exec_report` reads counters
  (s_ws etc.), not s_reg. `nv2a_raw_batch_hook` has **no setter
  anywhere in the tree** — it is never installed, so the
  `rb->regs = s_reg` hand-out has no hook consumer. RECOMP_PEEK
  reads guest RAM, not the shadow. The menu/debug tooling reads no
  regs (grep: no reg_shadow/->regs use under src/nv2a_dk/ beyond
  the backend's own).
- **The slow path's own cases** (exec_dk.c:2269/2297/2305):
  exec_method's INLINE_ARRAY / AE16 / AE32 cases touch only
  s_gpu (the same index/inline products the fast path builds).
  The shadow is maintained for these methods on the slow path
  solely by the prologue's shadow_write.

**The dirty-flag side.** Block 0x60 also carries DRAW_ARRAYS
(0x1810, word 0x604), which passes through exec_method's prologue
shadow_write on every draw batch in this title's D3D pattern (its
count/start word changes per batch in practice). The block's
transport therefore does not hinge on shadow_run: in the patrol
pattern it is dirtied by the draw method itself, and state_write
copies it (with whatever the three data words then hold) into the
record stream regardless. Skipping shadow_run does not save the
64 B copy and does not change which blocks travel.

**The one real cost of skipping, stated plainly:** backend.h's
documented contract is "regs[m/4] is the last value the title
wrote to 3D-class method m, the same register file the hardware
draws from". For three data-port words that contract stops being
kept — their hardware "registers" are write-only data ports whose
last-word value has no meaning, and no in-tree consumer reads
them (audit above), but a future diagnostic written against the
documented contract would read a stale word. That is the whole
risk, and it is why the skip ships behind a default-off toggle
with this audit attached, and why the header comment gets a
carve-out line if the toggle ever defaults on.

**Verdict per method class:**

| Method | Value readers | Verdict |
|---|---|---|
| ARRAY_ELEMENT16 (0x600) | none | **SKIP LIVES** |
| ARRAY_ELEMENT32 (0x602) | none | **SKIP LIVES** |
| INLINE_ARRAY (0x606) | none | **SKIP LIVES** |

One implementation condition (from the first study, confirmed in
code): parity with RECOMP_PB_FAST=0. The slow path maintains these
words per word in exec_method's prologue; the toggle's skip must
be applied there too (a three-method carve-out in the prologue's
shadow_write call) or the two paths' shadows diverge while the
toggle is on. With the carve-out, both paths agree: the three
words simply stop being maintained while the toggle is on.

## 1.2 B.3 — Skip shadow maintenance for the bulk data methods (REPRICED, gate cleared)

- **Mechanism.** Delete the three `shadow_run` calls in exec_run
  (exec_dk.c:2541, :2559, :2569) under the toggle, plus the
  exec_method prologue carve-out (§1.1). Nothing else changes: the
  index/inline products are built exactly as today.
- **Price.** Ceiling = shadow_run's measured self share, 2.6% of
  active compute ≈ **0.42 ms**. The loop is a serial load/XOR/OR/
  store over every index word (~85–125K in heavy windows) and
  every inline word; the unpack that shares those cache lines
  stays, so the reach is most of the ceiling: **realistic
  0.3–0.4 ms** — firmer than the first study could state, because
  the gate it waited on is now closed.
- **Risk.** Low. Audit-complete (§1.1); the exposure is the
  documented-contract caveat, not a consumer. Toggle
  RECOMP_PB_DATASHADOW (absent/OFF = shipped behaviour).
- **A/B spec.** Dwell A/B (shadow_run's share must leave the
  profile; exec_run's share must not grow — the two are adjacent
  lines in the same cases) + patrol (walk µs/draw primary) +
  frame-exact replay (the products are untouched, so replay must
  be bit-exact; any divergence kills the item, it does not get
  explained away).

## 1.3 B.1 — Replace the fence pre-scan with an O(1) predicate (REPRICED down on scope)

- **Mechanism (verified against scan.c:682–701).** Before handing
  a non-index bulk run to exec_run, scan loops over all n words
  computing each word's method to test for 0x1D70 (and to feed
  note() under RECOMP_PB_SCAN). Replace with the header-only
  predicate: non-increasing run → `method == 0x1D70`; increasing
  run → `method <= 0x1D70 < method + 4*n` (0x1D70 and all methods
  are 4-aligned, so no alignment term is needed). Keep the loop
  only when s_scan_enabled (it exists for note()'s inventory);
  the per-word fallback for genuine fences is untouched, and
  g_pb_cmd_va is assigned in the fallback loop (:713), not the
  pre-scan, so fence logging is unaffected.
- **Price (corrected scope, §0.1).** The pre-scan's population is
  the DEKO3D bulk path only: in heavy windows ≈55K of ~145K words
  — dominated by SET_TRANSFORM program/constant runs
  (~30K words) — each getting a full extra read + method
  computation before vp_upload_run / the per-word loop reads them
  again. It is a sub-share of scan's 11.4% (~1.8 ms ceiling):
  **realistic 0.1–0.3 ms**, plus unpriced cache relief for the
  pass that follows. (The first study's "few tenths" survives,
  its implied population does not.)
- **Risk.** Near zero — the predicate is exact, and its one trap
  is covered: a standalone count-1 fence header never reaches
  this path with n>1 semantics changed (the predicate evaluates
  it identically: noninc method==0x1D70 → has_fence → fallback,
  as today).
- **A/B spec.** Toggle RECOMP_PB_FENCESCAN (absent/OFF = shipped).
  Dwell A/B for scan's share + patrol + replay. A fence-heavy
  sanity scene is the boot/title sequence itself (fences park in
  segment tails — scan.c:761 comment): the replay route covers
  it.

## 1.4 B.2 — NEON the ARRAY_ELEMENT16 unpack (CONFIRMED, with the split counter added)

- **Mechanism (code as read, exec_dk.c:2521–2543).** Per guest
  word the scalar loop does: two mask/shift extractions, a
  min/max pair (two csel pairs), two u32 stores — ~10–12 ops —
  over ~85–125K index words/frame in heavy windows (AE16's share
  of them per §0.2). NEON form: per 8 guest words, two vector
  loads, ushll/ushll2 widening to 16 u32 stores, umin/umax
  accumulation with one horizontal reduction per run; scalar
  tail; the k-clamp against s_gpu.idx capacity
  (`(NV_MAX_INDICES - idx_count)/2`) and the lo/hi seeding from
  the batch's running range are preserved exactly, as is the
  write-everything behaviour under `s_gpu.prim` (no
  "skip stores for discarded draws" smuggled in — that would be
  a separate, argued change).
- **Counter addition (part of this card, two lines).** Split
  s_ws.idx into idx16/idx32 at the two cases (exec_dk.c:2543,
  :2561) and print both in the [perf] pushbuffer line. The A/B
  then measures the split it prices, instead of inheriting the
  uncertainty the first study had to carry.
- **Price.** Ceiling = the loop's home, exec_run's 15.9% self
  share (~2.5 ms). At a 2–4× loop speedup, **realistic
  0.5–1.0 ms** stands — still the largest decode-side number.
  AE32 (exec_dk.c:2544–2561: copy + min/max per word) gets the
  lesser vector treatment inside the same toggle only if the
  split counter says its words are worth it; its per-word cost
  is already ~3× lower.
- **Risk.** Medium — hand NEON in the executor's hottest path.
  The gate is the desk harness: scalar vs NEON over randomised
  runs including clamp truncation, n=0, tail lengths, and
  prim==0, requiring bit-exact idx[] + lo/hi + count. Any
  divergence kills the item at the desk. (The first study's
  zero-NEON disassembly finding was on the 5b0e0ec build tree;
  the source files are unchanged at 53a83f2, and the check is
  re-run at implementation time as the study instructed.)
- **A/B spec.** Toggle RECOMP_PB_NEONIDX (absent/OFF = shipped).
  Harness first; then dwell (exec_run share), patrol, replay
  bit-exact.

## 1.5 B.4 — vp_upload_run row batching (cross-reference, not re-owned)

Stands as the first study priced it (~0.2 ms realistic of a
~0.42 ms ceiling; the const dirty-range bookkeeping is the
correctness surface). The [perf] word split (§0.2) supports its
population: programs + constants are ~20–25% of heavy-window
words (~30K/frame). It remains sequenced **inside Surgery 1**
(state-path changes in one pair of hands) and appears in the
combined list (§3) as a cross-reference so the ordering is
complete. Not re-designed here.

## 1.6 B.5 — Header/window merge: DOWNGRADED, closed as a slot

The first study priced this before reading scan's current shape.
The bulk paths already resolve a run's parameter window **once
per command** (scan.c:666–681, with the comment recording that
per-word pb_word "was over 2% of the GPU thread" — the fix this
item imagined is the code as shipped). What remains for pb_word
(1.6% self) is header fetches (one per command; commands are
≤2047 words by the header count field, scan.c:628) and
straddle/past-end words in the fallback loop. Residual ceiling
≤0.05–0.1 ms, in fiddly window/VA bookkeeping. **Not a surgery
slot.** If a worker is already inside scan for B.1 and the diff
stays tiny, folding the header fetch into the params resolution
is permitted as a rider; it earns no A/B of its own.

## 1.7 Decode dead ends (reaffirmed / new)

- **Scan/walk merge:** already the architecture (bulk handoff).
- **Optimisation flags for the executor files:** bounded by the
  campaign's S2 arm (whole-runtime −O3: walk ~2.5% cheaper per
  draw, zero frames). Not a slot.
- **The straddle-gather** (scan.c:656–661: index runs crossing
  snapshot windows are gathered word-by-word through pb_word):
  frequency unmeasured, runs are ≤2047 words and windows are
  large; a one-counter measurement is the only honest next step,
  and it is a rider on B.2's counter change, not a candidate.
- **batch_index_range vectorisation:** cold fallback, unreached
  in volume at the patrol.
- **Lookup/caching anything:** §4.

---

# 2. DATA MOVEMENT

## 2.1 The memcpy census — which copies carry the 16.9%

Dwell chain: `memcpy < q_draw < raw_batch < nv2a_pb_exec_method`,
16.9% of active walk compute (~2.7 ms ceiling). The [QUEUE]
accounting (dwell, busy Stg30 windows) splits the bytes; the walk
pays the producer-side copies in q_draw:

| Copy | Bytes/frame | Paid where | Notes |
|---|---|---|---|
| Vertex array ranges (merged [lo,hi] per attribute) | 3,464–3,697 KB | q_draw (queue.c, local-range memcpy loop) | The prize: ~62% of record bytes. Pure relocation under shipped flags (converted attrs = 0 KB proves no conversion). |
| Index array (executor-made u32) | 893–949 KB | q_draw (queue.c:570) | Derived data (unpacked + DRAW_ARRAYS-expanded), not a guest copy. §2.3. |
| Remainder (state + batch transport) | ≈1.1 MB | q_draw / state_write / runs_write | Split from code in §2.2. |
| INLINE_ARRAY staging | (inside exec_run, counted in decode's word flow) | exec_dk.c:2566 memcpy into s_gpu.inline_buf, then again as a "local" range in q_draw | Inline words are ~0% of heavy-window words at the patrol; two small copies of a small payload. Not a target. |

Not on the walk, therefore not in the 16.9%: raw_batch's rebase
copy (bypassed under the shipped RAW_ABS_INDICES flag —
rb.indices points at s_gpu.idx directly), run_draw's batch
rebuild, and the DK backend's upload copy + NEON index rebase in
dk_draw_raw_body (renderer thread, 77% idle — real work, wrong
thread, F1 priced the whole renderer at ~3 ms overlapped).

## 2.2 A.4 — The ≈1.1 MB remainder, split from code (CLOSED as a measurement gate)

Per-record anatomy from the structs as compiled (host probe
against rc042's nv2a_backend.h: sizeof(Nv2aRawBatch) = 928,
B_HEAD = 264, tail part = 24, direct entry = 24 B, attr_const =
256 B; queue structs: QHdr 16, QBlock 80, QRun 16 + 16/row,
QDraw 16). At ~855 records/frame (798–913):

| Component | Per record | Per frame (est.) | Basis |
|---|---|---|---|
| Fixed batch transport (QDraw 16 + B_HEAD aligned 272 + tail 32 + direct entries 24 × nd≈4–8) | ~420–510 B | **~0.36–0.44 MB** | Struct sizes × record count; nd from the patrol's attribute profile |
| Shadow blocks (80 B each; state_write, queue.c:383) | ~3–5 blocks | **~0.20–0.35 MB** | Dirty-set size per draw interval; bounded, not counted in any log |
| VP program/const rows (16 B/row + run headers; runs_write) | varies | **~0.10–0.20 MB** | Prog+const words ~30K/frame in heavy windows (§0.2); only changed rows ship, diffed against the producer mirror |
| attr_const block (256 B, only on generation change) | 0 or 256 B | **0–0.22 MB** | send_attr is generation-gated (q_draw); churn rate is title-dependent |

Sum ≈ 0.76–1.21 MB — the code-derived split **closes against
the measured ≈1.1 MB** within its stated uncertainty. Verdicts:

- **No remainder sub-candidate justifies its own surgery.** The
  largest piece (fixed batch transport, ~0.4 MB ≈ ≤0.15 ms of
  walk ceiling) is structural: the record *is* the batch
  descriptor; shrinking it is a two-thread format change priced
  well above its ceiling.
- The runs_write mirror double-write (each changed row is
  memcpy'd into the record *and* into the p_prog/p_consts diff
  mirror) is inherent to diffing — the mirror must outlive the
  record. Not a trim.
- A.4's instrumented run (two counters: blocks/record, rows/
  frame — both are one-line additions beside the existing
  [QUEUE] accounting) is demoted from gate to optional
  confirmation: even at the extremes of the bounds above, no
  decision changes.

## 2.3 A.3 — u16 index transport (CONFIRMED, interface audit complete, price revised)

- **Interface audit (the first study's open item — closed).**
  Tree-wide, `b->indices` has exactly **two** consumers:
  q_draw's copy (queue.c:570) and dk_draw_raw_body's
  rebase/narrow loop (nv2a_dk.c:4549). run_draw re-points the
  batch at the record (assignment, not a read). No other
  backend, hook or diagnostic reads the array. The change
  surface is exactly producer + one consumer loop.
- **Mechanism.** When the batch's rebased range fits u16 — the
  same quantity the backend's format choice already keys on,
  (index_hi − index_base) = vertex_count − 1 < 65536 — q_draw
  writes pre-rebased u16s into a variant record layout (a flag
  bit on QDraw, mirroring the attrs_packed pattern); the
  backend's loop reads the narrow form and skips the subtract.
  Fallback per batch = today's path. (Implementation must
  re-read the backend's isz decision line and mirror its exact
  predicate — the consumer loop's shape at nv2a_dk.c:4549–4572
  is verified; the choice line above it was not re-read here.)
- **Price — revised down, with the reason.** The first study
  priced ~0.2 ms as "half the index copy". But today's walk-side
  cost is a straight memcpy of ready u32s; the u16 variant
  replaces it with a per-index subtract+narrow on the walk
  (the backend's NEON loop shape, moved threads) writing half
  the bytes. Byte volume halves; per-index work appears. Net
  **realistic 0.05–0.15 ms**, plus halved index ring traffic
  (not a constraint — 8 MB ring — and not sold as one).
- **Risk.** Medium-low. Exact range test per batch; indices are
  executor-owned (no guest-lifetime question); lives under both
  COPY_VERTICES settings.
- **A/B spec.** Toggle RECOMP_QUEUE_IDX16 (absent/OFF =
  shipped). Gate before any frame claim: the [QUEUE] line's
  index KB/frame must roughly halve in the ON arm. Then patrol
  + replay bit-exact.

## 2.4 A.5 — Free trims (stand as priced, ~0.1 ms total)

- **p_attr_const mirror copy is write-only** (queue.c:156
  declared; written in q_draw's send_attr block; read nowhere —
  full-file grep, re-verified at rc042). Delete the copy, keep
  the generation bookkeeping. Zero risk.
- **describe_state's unconditional attr_const copy**
  (exec_dk.c:1244: 256 B into every batch descriptor) vs its
  consumer gate (run_draw's attr_const_gen check): copy only on
  generation change against the last-described generation.
  ~218 KB/frame at patrol draw counts. Verify describe_state's
  diagnostic callers first, as the first study instructed.
- Together ~0.2 MB/frame ≈ **~0.1 ms**. Worth doing because
  they are free; sequenced as one small build.

**INLINE lifetime, closed (first study's open item).**
Attributes fed by INLINE_ARRAY point into the executor's own
staging (`s_gpu.inline_src`, marked attr_direct_local —
exec_dk.c:1128, "the executor's memory, not guest"), and q_draw
copies local ranges into the record in **both** COPY_VERTICES
modes (queue.c header: "inline vertices always"). Nothing on
the shipped path references pushbuffer-snapshot memory past the
walk; the only references that escape the walk are guest-memory
ones, and only under COPY_VERTICES=0 — which is §2.5's subject,
not inline data's.

## 2.5 The zero-copy release mechanism — what a fix must do (A.1's honest route)

**The mechanism as designed** (queue.c header contract, :21–47;
all line cites queue.c unless noted). With COPY_VERTICES=1
(shipped), draws commit as non-touching (`q_commit(size,
!s_copy_vertices)` in q_draw/q_clear), so guest-progress signals
pass at once. With =0, every draw/clear touches: `p_need`
(queue.c:~157) tracks p_head after the last touching record, and
the executor's progress surface is gated on the consumer's tail
passing it:

- `progress_store` (exec_dk.c:314) routes the semaphore release
  (0x1D70) and the PATT_COLOR0 latch (0x0310) through
  `nv2a_backend_queue_store` whenever the queue is on.
- queue_store / queue_call: if `now_ok()` (tail ≥ p_need) the
  store lands immediately; otherwise it is **queued as a ring
  record** behind the touching draws (st_stores_queued counts
  these; the copies-on control arm reads 0.0 queued, ~3.3/frame
  at once).
- `trap_deliver` (exec_dk.c:2068) calls
  `nv2a_backend_queue_sync(0)` — flush, then a hard executor
  wait for tail ≥ p_need — before the software-method sync.
- Flips commit as non-touching (q_flip) — **but ring order still
  serialises any queued store behind a flip record**: the
  consumer makes records strictly in order (q_thread loop), and
  s_tail advances per record made.

**What Leg 1 proved** (two boots, identical; logs preserved at
~/flip/pw-record/leg1/): the mode engages (banner at log :29);
9 draws are made; then a quiet, total stall — 0 pushbuffer
words/frame, executor in its ack-wait (asleep ~1,993 ms/block,
woken 0 times — **not** in a prod_wait: after the first block
every executor wait counter reads 0.00), guest threads alive
inside kernel waits, screen black. The only live block's queue
report is the fingerprint: **renderer thread idle 0%** (busy
inside a backend call, with ~2 records/frame of throughput),
executor waits only via traps (0.2/frame, 2.75 ms — the trap
syncs *returned*), stores 0.3/frame queued / 0.2 at once.

**The failure shape, desk-bounded.** The executor and the store
queue are exonerated as the *stuck* party; the guest is starved
of a progress signal; the consumer was inside an inner-backend
call it did not return from within the window. The cycle that
fits every observation: a queued progress store sits in the ring
**behind a Q_FLIP**, the consumer is blocked inside that flip's
present path on a completion that transitively waits for the
guest (this title's flip discipline is guest-driven — the
executor fields SET_FLIP_READ/WRITE/MODULO methods, exec_dk.c
:2330s), and the guest will not advance until the store lands.
Desk work cannot prove the present path's exact wait from the
armchair — and the fix design does not need it to: every
candidate cycle shares one structural property, **a guest
progress signal whose delivery depends on the consumer reaching
it in ring order, while the consumer's current call can wait on
the guest.** Ranked candidates for the instrument to split:
(i) the flip-overtaking cycle above; (ii) a trap-sync /
software-method interlock in the boot path (traps were live in
the stalling block); (iii) the big-record path (records > ring/4
are made on the executor after prod_wait(W_ALL) — a boot-path
interaction, bounded, least consistent with the evidence).

**What the fix must do** (requirements, in the order they must
be built):

1. **Instrument first.** The report already counts stores
   queued/at-once and waits by reason; add consumer-state
   telemetry (current record type + time inside the inner call,
   printed in the same [perf] block). One reproducing boot then
   names its wait instead of implying it. No fix attempt before
   this exists — Leg 1 is what guessing costs.
2. **Bound every hold.** A queued store/call must land within a
   bounded time even if the consumer is blocked: stores may
   **overtake flips** (a flip touches no guest memory — the
   header already states the principle; ring order is what
   denies it), i.e. progress delivery keys on "all *draws*
   before it made", not "all *records* before it made".
3. **No sync may wait on a cycle.** queue_sync's wait set must
   exclude any record class whose completion can depend on the
   waiting party's own progress (flips again), for the same
   reason.
4. **A failsafe, not a hang.** If a hold exceeds a bound, force
   the store through with a log line. The correctness
   degradation is the documented one (a texture rewritten
   behind a fresh fence may show a frame early) — bounded and
   visible, never a black screen.
5. **Soak before speed.** The fixed mode must survive repeated
   cold boots to title + full patrols + replay across several
   boots before any perf A/B is run. The failure class is a
   hang, and a hang in 1 boot of 20 is still a shipped hang.

**Where it lives:** nv2a_backend_queue.c (now_ok/p_need,
queue_store/queue_call, queue_sync, the consumer loop), with
call sites in exec_dk.c (progress_store :314, trap_deliver
:2075). The change is small in lines and large in consequence:
it rewrites the frame's ordering contract.

**Price, honestly.** Ceiling = the vertex share of the walk's
copies ≈ **1.7 ms** — the biggest single number in either act.
Against it stands the header's own history: the copy exists
because holding releases behind this queue cost **~16% of the
game thread** in ring-space spin in the configuration that
motivated it. A correct release mechanism re-opens that trade
with better terms (bounded, flip-overtaking holds), but the
patrol is producer-paced (the walk waits 68.4% of samples for
guest pushbuffer), so walk savings can be re-paid as guest
latency. **Net frame effect: genuinely uncertain, −something
to +1.7 ms.** It ranks last in §3 despite its ceiling: highest
risk, hang-class failure mode, unproven net.

## 2.6 Data dead ends (reaffirmed)

- **Copy relocation** collapses into A.1 or pays both copies
  plus the holdback (first study A.2 — stands).
- **Partial/changed-range vertex copies:** ranges already tight
  and merged; guest memory carries no change information.
- **Converted-attribute copy:** 0 KB/frame as shipped.
- **Renderer-side copies:** wrong thread (77% idle; F1).
- **Snapshot/pushbuffer copies:** 0.2% of active compute.
- **Word-level shadow-block deltas:** attacks part of a
  remainder now split (§2.2) and dominated by fixed transport;
  the collect scan costs more than the delta saves.

---

# 3. The combined surgery list (Acts 2 + 3, one order)

Uniform A/B spec unless an item says otherwise: patrol A/B
against the pooled **33.0 ms** baseline (combined 400–699
median + sub-buckets), **[perf] walk µs/draw as the primary
instrument** (sub-quantum walk savings are real even when the
bucket does not move — the campaign's S2 arm is the precedent),
stock-clock gate from each run's own [SWPERF] samples, pack
frozen, replay bit-exactness wherever product bytes are claimed
untouched. Every toggle: **absent/OFF = shipped behaviour
byte-for-byte**, per the house convention Surgery 1 used.

| # | Item | Act | Expected (realistic) | Risk | Toggle |
|---|---|---|---|---|---|
| 1 | B.1 fence predicate (§1.3) | decode | 0.1–0.3 ms | near zero | RECOMP_PB_FENCESCAN |
| 2 | B.3 data-method shadow skip (§1.2) | decode | 0.3–0.4 ms | low (audit closed) | RECOMP_PB_DATASHADOW |
| 3 | A.5 free trims (§2.4) | data | ~0.1 ms | zero / low | none (one small build) |
| 4 | B.2 NEON AE16 unpack + split counters (§1.4) | decode | 0.5–1.0 ms | medium (harness-gated) | RECOMP_PB_NEONIDX |
| 5 | A.3 u16 index transport (§2.3) | data | 0.05–0.15 ms net | medium-low | RECOMP_QUEUE_IDX16 |
| 6 | B.4 vp row batching (§1.5) | decode/state seam | ~0.2 ms | medium-low | inside Surgery 1's sequencing |
| 7 | ZC-FIX release mechanism (§2.5) | data | ceiling 1.7 ms, **net uncertain** | **high (hang-class)** | COPY_VERTICES=0 stays the switch; fix lands behind its own guard until soaked |

**Combined realistic reach, items 1–6: ~1.25–2.05 ms of walk
time** against class ceilings of ~5.9 ms (decode) + ~2.8 ms
(data) — consistent with, and slightly firmer than, the first
study's 1.2–1.9 ms for its subset (B.1 repriced down on scope,
A.3 down on transform cost, B.3 firmed by the completed audit,
B.4 now counted). At the plaza that is the size of one vsync
quantum at the median bucket: expect the verdict to be carried
by walk µs/draw first and the 400-bucket second, exactly as
Surgery 1's proof plan framed it. Items are independent
(disjoint functions and word populations — B.1's pass covers
non-index runs, B.3's loop covers index/inline runs, B.2
rewrites the unpack inside the same case B.3 de-shadows;
A.3 lives under both COPY_VERTICES modes) and can land in any
order, but the sequence above front-loads certainty per unit
risk. ZC-FIX is a programme of its own (instrument → reproduce
→ fix → soak → measure) and must not be bundled with any item
above.

---

# 4. Non-targets and exonerations (do not spend here)

- **Lookup/caching — 4.2% of active compute (~0.7 ms total
  ceiling).** The dwell's proven non-target: win_match 1.9% is
  the largest single lookup; find_texture/find_vp/find_surface
  are already O(1) hashed with generation checks. Nothing in
  either act touches this class.
- **State application (39.0%)** is not exonerated — it is
  **owned by Act 1 / Surgery 1** (desk-complete: NOTE_TRIM,
  STATE_TRIM), including note_unhandled's bookkeeping tail.
  Listed here so it is never double-booked as decode upside.
- **Alpha test** (F2: 0.0 ms frame; its ~1.6 ms is GPU-side and
  the GPU is not pacing), **alpha-test proof** (F3: <1 ms; its
  product is holding dynamic scale at x1.50), **texture check**
  (F4 dose–response: priced ≈0.25 ms/frame at default, absorbed
  by renderer idle at every setting), **TRACE** (F5: certified
  nil), **hitch threshold** (F6: certified null; no 17–25 ms
  frame population exists on this pattern).
- **The renderer draw path as a frame lever** (F1: deleting the
  *entire* renderer buys ~3 ms; it runs inside 77% idle,
  overlapped, not pacing), **resolution** (C2/C3: x1.00 and
  x1.50 frame-identical — free at 99th), **GPU_MS** (C2: exact
  null), **runtime −O3** (S2: walk −~2.5%/draw, zero frames).
- **The walk's waits themselves** (68.4% of walk-thread
  samples): the executor is starved for guest pushbuffer, paced
  by production. That is the frame's shape, not a lever inside
  either act — and it is why ZC-FIX's net is uncertain (§2.5).

---

# 5. What was NOT verified

- **The exact Leg 1 wait cycle.** Desk-bounded to "consumer
  inside a backend call + guest starved of a queued progress
  signal, flip-overtaking the prime suspect" (§2.5); the
  instrument specified there is what settles it. Not asserted
  as proven anywhere in this study.
- **The AE16/AE32 word split.** Bounded (indices 61–67% of
  heavy-window words; sampler hot line in the AE16 loop) but
  not counted; B.2's card adds the counters.
- **Blocks/record and vp rows/frame empirically.** The §2.2
  split is code-derived and closes against the measured total;
  its two estimated rows (blocks, rows) would be pinned by the
  optional counters named there. No decision waits on them.
- **The backend's isz decision line** for A.3's range predicate
  (consumer loop verified at nv2a_dk.c:4549–4572; the choice
  above it must be re-read at implementation so the record-side
  test mirrors it exactly).
- **exec_run's compiled shape at 53a83f2** (zero-NEON): source
  files are byte-identical to the disassembled 5b0e0ec tree
  (git diff, above), so the finding carries; B.2's card still
  re-checks the ELF it builds.
- **Straddle-gather frequency** (§1.7) and the fence pre-scan's
  exact sub-share of scan (bounded in §1.3, not instrumented).
