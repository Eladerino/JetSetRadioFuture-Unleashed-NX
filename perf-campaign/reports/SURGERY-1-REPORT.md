# Walk Surgery 1 — bookkeeping trims (desk report)

Fri Nov 6, 2026 (work done Nov 5). Desk phase only: **zero console contact,
nothing pushed.** Source for the targets: WALK-ATTRIBUTION-2026-11-05.md
(shares below are of the walk thread's *active compute*; ceilings are against
the ~16 ms walk wall).

- **Base:** main `5b0e0ec` (v0.4.1-switch). **Fork:** `~/jsrf-nx-surgery1` on
  the LOQ, commit **`0c08e2f`**, own `WORK_DIR=$HOME/jsrf-switch-build-surgery1`.
- **One file touched:** `src/kernel/nv2a_pb_exec_dk.c`, +70/−4. Line numbers
  below are the patched file unless marked pristine.
- **Patch:** `surgery1.patch` beside this report (5,251 B, sha256
  `e1ae6beafd0df4b5d97035a99dedc4371fbd622e0db3e02f983b92c4d6e61e33`),
  apply-verified against pristine 5b0e0ec (patched tree `0fef2e6f…` ==
  commit tree). `git fsck --strict` clean.
- **Build:** NRO **44,882,117 B, sha256 `8993f1a24f1347f1…`**, version string
  `0.4.1-switch-1-g0c08e2f`, at
  `~/jsrf-switch-build-surgery1/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
  on the LOQ. The previously staged NRO was snapshotted before the build
  (`~/jsrf-surgery1-scratch/staged-before.nro`, `2acb6e61…`). Note:
  Downloads/jsrf-unleashed-switch was re-staged over by another build at
  01:15 Nov 6 (`ea66f90f…`) — the surgery product survives in its own dist
  dir, which is the copy to use.
- **Desk-proven:** the code change, the capture-equivalence harness, the
  patch, the build. **Not proven:** any timing effect, and the build has not
  been booted. All timing claims wait for the hardware proof below.

## Target 1 — the unhandled-method path (`RECOMP_DK_NOTE_TRIM`)

Dwell: `note_unhandled` 5.2% + `unhandled_on` 0.6% + `rest_note` 0.6% ≈ 6.4%
of active compute, ceiling ~1.0 ms.

### What the time actually was

Not string formatting and not log writes — **per-word table maintenance**,
paid on every word the executor has no handler for, in a configuration where
noting was fully on (the campaign baseline options carry no RECOMP_QUIET;
the dwell log's own [GPU] reports show 346,081 unhandled words and 227
distinct methods by the second report).

Per call, `note_unhandled` (patched :413) did: the `unhandled_on()` gate
(:405 — a call chain over two cached statics, `!xbox_log_quiet() ||
pb_verbose()`), a slot-table load (`s_unhandled_slot`, 2,048 entries), an
`s_gpu.unhandled_total` read-modify-write, and either a per-method count
RMW + `last_param` store in `s_unhandled`, or an append under the
PB_EXEC_MAX_UNHANDLED cap. The dominant caller is the learned-plain-word
fast loop in `nv2a_pb_exec_run`, which **double-gates**: it checks
`unhandled_on()` itself and then `note_unhandled` checks it again.
`rest_note` (:2020) is a separate per-non-plain-word tax — an inlined load
and branch on `s_rest_on` in both exec_run loops — whose counts are only
ever read when RECOMP_PB_REST=1 (default off).

### What consumes the notes

Exactly one reader: `nv2a_pb_exec_report` (:2852 region) — the [GPU]
report's total, distinct count, and top-10 per-method ranking. It is
skipped entirely when quiet, and called only from the scan loop / verbose
flip counter. Nothing else reads `s_unhandled` or `unhandled_total`: the
[perf] line uses the separate `s_ws` counters, the menu never touches it.
`s_rest` is read only by `nv2a_pb_exec_rest_top`, itself gated on
RECOMP_PB_REST. So the per-word cost buys a diagnostic ranking that is
printed at most every 600 flips in verbose runs.

### What changed (toggle `RECOMP_DK_NOTE_TRIM=1`, getter at :58)

- `note_unhandled` fast head (:417): under the toggle, one
  `s_gpu.unhandled_total++` and return — no gate call, no slot, no table.
  Its three `exec_method` call sites are untouched; they get the trim
  through the function.
- `nv2a_pb_exec_run`: the toggle is hoisted into a local (:2568). In the
  plain-word loop, noting becomes a register counter (`trim_noted`,
  :2641) folded into `unhandled_total` once per run (:2651) — legal
  batching because the total's only reader is the report, which never
  runs mid-run. The two `rest_note` call sites are skipped under the
  toggle.
- The report prints one extra line under the toggle stating the
  per-method ranking is off; **the printed total stays exact**.

Semantics: with the toggle absent/0 the shipped path runs as before (the
only residue is one predictable branch per plain word for the hoisted
local). With it on, the one deliberate loss is the per-method ranking and
distinct count; NOTE_TRIM also supersedes RECOMP_PB_REST (same class of
bookkeeping — documented at the getter).

Scope note: the GL executor `nv2a_pb_exec.c` has its own twin noting and
is untouched — the dwell measured the DK executor, and this surgery is
scoped to it.

## Target 2 — the state-capture quartet (`RECOMP_DK_STATE_TRIM`)

Dwell: `regs_collect` 1.9 + `capture_transform` 1.3 + `capture_render_state`
1.3 + `rows_diff` 1.3 + `state_write` 1.3 + `describe_state` 1.0 ≈ 8% of
active compute; the dwell put the quartet proper's ceiling at ~0.8 ms.
The audit's headline: **most of that is load-bearing delta machinery.**
Exactly one capture is largely dead.

### `capture_render_state` (:1693) — 17 of 19 cases write fields nobody reads

Field-by-field consumer audit across `src/` and `port/`: of the 19 cases,
only two have a reader in this build —

- `0x0214` SET_SURFACE_ZETA_OFFSET → `rs->zeta_va`, read by the clear
  body (`nv2a_dk.c`, the only rs field it reads) and by `describe_state`
  (→ `rb->zeta_va`);
- `0x1D8C` SET_ZSTENCIL_CLEAR_VALUE → `s_gpu.zstencil_clear` (not an rs
  field), read at the `clear_surface` call.

The other 17 cases — the 15 RS_WORD fields (alpha_test_enable,
blend_enable, cull_enable, depth_test_enable, alpha_func, alpha_ref,
blend_src, blend_dst, blend_eq, blend_color, depth_func, color_mask,
depth_write, cull_face, front_face) plus depth_min/depth_max
(SET_CLIP_MIN/MAX) — are written on every occurrence and read by nobody:
`Nv2aRawBatch` (nv2a_backend.h) carries no render-state struct at all,
and the deko backend derives all draw state from the register shadow.

**Change** (toggle `RECOMP_DK_STATE_TRIM=1`, getter at :69): at the head
of `capture_render_state` (:1706), the 17 dead methods — encoded as a
bitmask over (method − 0x0300)/4 on [0x0300, 0x03A0], a range containing
neither live case — return 1 without the store. The methods stay *taken*
exactly as shipped: not marked plain, not counted unhandled, shadow still
written. Only the dead store goes. Honest size: that is a fraction of
capture_render_state's 1.3% — the dispatch itself stays — so this toggle
is expected to sit under patrol resolution on its own; it exists so the
proof can price it instead of assuming it. The mask constant
(`0x1E000FF800F`) was regenerated programmatically from the case list in
the source and matched.

### Deliberately NOT changed, and why

- **`capture_transform`** — fully load-bearing: composite[16] is copied
  into every batch by `describe_state`; vp_offset feeds `rb->vp_offset`
  *and* vertex-program constant c[59]; xform_mode selects
  batch_is_vp/batch_is_ffp.
- **`describe_state`** — two candidate trims rejected as unprovable:
  (a) the 256 B attr_const memcpy: consumers key on `attr_const_gen`,
  but their staleness is invisible to the executor, so skipping the copy
  could hand a stale-gen consumer zeros; (b) caching the per-stage
  `dma_resolve` calls: DMA-object mapping is title-reprogrammable with
  no generation counter to key a cache on (and in this executor
  `dma_resolve` is a compare, not the RAMHT walk — cheap already).
- **`regs_collect` / `rows_diff` / `state_write`** (nv2a_backend_queue.c)
  — already the minimal mechanism: 128 dirty register blocks collected
  and cleared per record, row-level memcmp diffs, and vertex-program
  constants already have a generation-gated range path that skips the
  diff entirely in steady state. Every collect feeds exactly one write.
  The one wart found — `q_clear` copying the whole render-state struct
  into its record while the consumer reads only `zeta_va` — was left:
  clears are single-digit per frame, and changing the record format
  risks consumer compatibility for ~72 B a clear.

## Desk verification

- **Host harness** (`~/jsrf-surgery1-scratch/captest.c`, host cc): the
  patched `capture_render_state`, extracted from the real source,
  against the pristine function over **all 2048 methods × 5 params**.
  Toggle OFF: 0 mismatches in return value, full rs bytes, and
  zstencil_clear. Toggle ON: 0 mismatches in return value, `zeta_va`,
  and `zstencil_clear`; exactly **85 runs differ** (17
...[truncated 2705 chars]