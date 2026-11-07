# STATE SURGERY BUILD — S-2 + S-3 + STATE_TRIM (desk record, Nov 6, 2026)

Desk only. No console contact, nothing pushed. Tree: LOQ fork
`~/jsrf-nx-s23`, copied from `~/jsrf-nx-rc042` @ `53a83f2`.
Spec: STATE-STUDY.md §3 (S-2, S-3) + the STATE_TRIM addition
(Surgery 1 port). §4 non-targets respected throughout: nothing
stacked onto STATE_TRIM's fraction twice, the value-gated shadow
(§2.1) untouched as a mechanism.

## Commits (local only, Eladerino <glynnbrandon59@gmail.com>)

| Commit | Item | Diff |
|---|---|---|
| `4c975ee` | Campaign base: menufix.patch applied to rc042 | 1 file, +23 (verified exactly: `src/third_party/uam/source/uam_api.cpp`, the uam compile mutex) |
| `b044f6c` | S-2 — `RECOMP_DK_CAPTURE_FAST` | `nv2a_pb_exec_dk.c`, +30/−1 |
| `61de7f7` | S-3 — `RECOMP_DK_DIRTY_LIST` | 3 files, +89/−2 (exec_dk.c, nv2a_backend.h, nv2a_backend_queue.c) |
| `25f491f` | STATE_TRIM port — `RECOMP_DK_STATE_TRIM` | `nv2a_pb_exec_dk.c`, +30 |

Total vs rc042: 4 files, +172/−3 (re-verified after the build,
tree clean — see the build note below).

Every toggle follows the tree convention: cached `getenv` read,
absent (or 0) = shipped behaviour byte-for-byte, so the control
arm is this same NRO with no toggles set.

## S-2 — capture-chain dispatch pre-test

The default case in `nv2a_pb_exec_method` now routes its first
chain call through `capture_render_state_fast` when the toggle is
on. The pre-test is the switch's own taken set, derived from the
switch itself (not the study's list): in-cluster range
0x0300–0x03A0 plus the two strays 0x0214 / 0x1D8C. It is a
superset of the cases — in-cluster methods the switch does not
take still enter and return 0 — so taker and side effects are
unchanged; miss words (the chain's majority) skip the call and
its 19-case switch.

**Desk gate (captest pattern, verbatim-extracted function +
wrapper):** all 8192 method values × 6 representative params =
**49,152 cases, 0 mismatches** in return value, the full
render-state struct (byte compare), and `zstencil_clear`. Taken
set measured from the shipped switch: exactly 19 methods
(15 RS_WORD + SET_CLIP_MIN/MAX + the two strays), all inside the
pre-test; 114 taken-calls (19 × 6 params).

## S-3 — dirty-list collection

`shadow_write` / `shadow_run` now set flags through
`shadow_block_dirty`, which appends the block index on the flag's
0→1 transition when the toggle is on. `regs_collect` drains the
list via the new `nv2a_pb_reg_dirty_collect` (exported in
nv2a_backend.h); the shipped 128-flag scan is retained in full
for the toggle-off path. The two invalidation points set the
reseed flag: the initial all-dirty state (reseed starts set —
the static initializer at exec_dk.c:303 has no list) and
`q_draw`'s failure-path memset (queue.c:520, now also calls
`nv2a_pb_reg_dirty_reseed()`). A reseeded collect runs the
shipped scan, clears the flags, and restarts the list empty —
the correct rebuilt state, since every collect leaves all flags
clear. Appends are skipped while reseeded, and the list is
bounded by construction (append only on 0→1; ≤128 entries).
List order is append order, not scan order; the consumer
applies disjoint 64-byte regions into `c_regs`, so order is
free (STATE-STUDY §S-3). Producer and collect run on the same
executor-side thread as the shipped flags assume.

**Desk gate (verbatim-extracted machinery vs an independent
shipped-semantics reference model, randomized streams):**
**149,923 producer ops, 50,081 collect windows (36,600
non-empty), 5,997 reseed events — 0 mismatches.** Every window
compared on the emitted block SET, the full shadow contents,
and the flag arrays. Scripted windows included: initial
all-dirty collect, change-and-change-back within one window
(block still ships, both models), and a no-change
`shadow_run` (differed == 0 dirties nothing).

## STATE_TRIM — Surgery 1 port

Verbatim port of the STATE_TRIM half of surgery commit
`0c08e2f` (getter + the bitmask early-return at the head of
`capture_render_state`, mask `0x1E000FF800F` over
(method − 0x0300)/4). NOTE_TRIM was **not** ported — it lands
in a different fork. The 17 masked cases are still taken
(return 1); only their dead stores go. The two live cases
(zeta_va / zstencil_clear) are outside the mask and execute
exactly as shipped.

**Desk gate (re-run on this base; the old fork's result was
not inherited):**
- OFF vs the pristine function extracted from git HEAD:
  8192 methods × 6 params = **49,152 cases, byte-identical**
  (return, full struct, zstencil_clear), 0 mismatches.
- ON vs OFF: 2048 word-aligned methods × 6 params = 12,288
  records; **exactly 102 records differ = the 17 dead methods
  × 6 params**, differing methods are precisely
  0x0300…0x03A0's 15 RS_WORDs + 0x0394/0x0398, and every
  differing byte lies in a dead field — return value, zeta_va
  and zstencil_clear identical everywhere. **0 violations.**

Harness sources: `s23-harness/` beside this report
(`captest_s2.c`, `dirtytest_s3.c`, `trimtest.c`; also on the
LOQ at `~/s23-harness/`). All three harnesses test code
extracted verbatim from the patched tree, never a retype.

## Build

Exact prescribed command (WORK_DIR pinned to this fork):
`WORK_DIR=$HOME/jsrf-switch-build-s23 JSRF_GEN_OPT=-O3 SKIP_RECOMP=1 tools/switch/build.sh "<ISO>"` — completed
(`BUILD-DONE`), version string `0.4.1-switch-9-g25f491f`.

- NRO (LOQ): `~/jsrf-switch-build-s23/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
- Size: **44,914,885 bytes**
- sha256: **7ebe5931104be285529d92595ee95f625084ed79dfefad890f4e2769f9cc17ab**
- All three toggle strings present in the binary (strings check).
- Post-build tree re-verification passed: HEAD `25f491f`,
  clean, +172/−3 vs `53a83f2` (the LOQ rollback guard).

Build-environment note, for the record: the LOQ's WSL instance
died six times during this build's window (OOM-killed cc1 under
three concurrent campaign builds, then repeated instance deaths
at ~10-minute intervals even solo). Two of my own errors are in
that trail and are owned here: an early resume attempt died by
SIGHUP with its launching session, and a direct `cmake --build`
retry loop initially lacked build.sh's `DEVKITPRO` export
(13 fast-failed attempts, no tree impact). The successful run
was the prescribed build.sh command, resuming incrementally on
persisted objects; the NRO above is its product, hash- and
version-verified against the tree after completion.

## Patrol A/B arm spec (hardware, when a window is granted)

House standard per STATE-STUDY §3.0, unchanged: 99th Street
entry-plaza patrol, blueprint replay, Game Data 1; pooled
baseline **33.0 ms median**; stock-clock gate (the run's own
[SWPERF] samples uniformly 1020/384/1331); shader pack frozen;
one variable per arm; verdict on frame-time buckets AND the
[perf] walk figure (µs/draw); **replay frame-exactness is the
correctness gate for every arm** — any divergence voids the
arm, and for S-3 a missed block is a wrong picture by
construction. A `sample=1` dwell re-run prices any share claim.

| Arm | Toggles | Prices |
|---|---|---|
| Control | (none set) | must reproduce 33.0 ms pooled |
| S-2 | `RECOMP_DK_CAPTURE_FAST=1` | 0.1–0.3 ms expected |
| S-3 | `RECOMP_DK_DIRTY_LIST=1` | 0.1–0.25 ms expected |
| STATE_TRIM | `RECOMP_DK_STATE_TRIM=1` | Surgery 1's fraction of capture_render_state |
| S-2 + S-3 | both | dispatch + enumeration |
| All three | all | combined state-class reach of this build |

Order suggestion: control → S-3 (strongest correctness signal
per ms at stake) → S-2 → STATE_TRIM → combos. Effects this
size sit under one frame quantum at the plaza; judge on the
walk µs/draw figure first, buckets second, exactly as the
Surgery 1 A/B was specced.

## Not done / not claimed

- No hardware run of any arm (desk only, per the commission).
- No push anywhere; the four commits are local to the LOQ fork.
- S-3's producer/consumer threading assumption is the shipped
  flags' own (single executor-side producer/collect); stated
  in the code comment, not separately proven.
- STATE_TRIM's reader audit is Surgery 1's (zeta_va /
  zstencil_clear the only consumers), re-verified here only
  to the extent of the ON/OFF harness above.
