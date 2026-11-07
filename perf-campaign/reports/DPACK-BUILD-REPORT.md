# CAMPAIGN DECODE PACK — build report (desk only)

Fri Nov 6, 2026. **Desk phase only: zero console contact, nothing
pushed.** Spec: DECODE-DATA-STUDY.md — items B.1 (§1.3), B.3 (§1.2,
audit closed §1.1), A.5 (§2.4), plus Surgery 1's NOTE_TRIM (study
§0.3, decode-classed, owned by Surgery 1's patch).

- **Tree:** LOQ fork `~/jsrf-nx-dpack`, copied from `~/jsrf-nx-rc042`
  @ 53a83f2. Five commits, all local, author Eladerino
  <glynnbrandon59@gmail.com>:

  | Commit | Item |
  |---|---|
  | `99c15fb` | Campaign base: menufix on the RC tree |
  | `2f09f1c` | B.1 fence predicate (`RECOMP_PB_FENCESCAN`) |
  | `0013dfe` | B.3 data-method shadow skip (`RECOMP_PB_DATASHADOW`) |
  | `73f6d4e` | A.5 free trims (no toggle; own commit) |
  | `8d93d0c` | NOTE_TRIM (`RECOMP_DK_NOTE_TRIM`) |

- **Build:** `WORK_DIR=$HOME/jsrf-switch-build-dpack JSRF_GEN_OPT=-O3
  SKIP_RECOMP=1 tools/switch/build.sh "<USA ISO>"`, exit 0 (the first
  held session dropped at 95%; the resume completed — all four edited
  objects plus uam_api.o show as compiled in the logs, and the tree
  was re-verified after the build: status clean, log intact,
  `git fsck` no errors beyond pre-existing dangling commits inherited
  from the rc042 clone).
- **NRO:** `~/jsrf-switch-build-dpack/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
  on the LOQ — **44,910,789 B, sha256
  `aff65eae593cf5b623e97411e4c9dab80dc5a5497c80b414089a8c1e3be7c965`**,
  version string `0.4.1-switch-10-g8d93d0c`. All three toggle names
  and the NOTE_TRIM report line are present in the binary's strings.
- **Desk-proven:** the code changes, the three gates below, the
  build. **Not proven:** any timing effect; the build has not been
  booted. All timing claims wait for the patrol A/B in §5.

## 0. Campaign base (commit 99c15fb)

`menufix.patch` (from the proof window's anomaly folder) applied to
the rc042 copy: `git diff --stat` = **exactly one file,
`src/third_party/uam/source/uam_api.cpp`, +23/−0**, as specified, and
committed locally as the campaign base. This is the control build
for the A.5 arm (§5) and the tree every toggle's OFF state must match
behaviourally.

## 1. B.1 — O(1) fence predicate (commit 2f09f1c, scan.c +26/−0)

Toggle `RECOMP_PB_FENCESCAN` (cached getenv getter, house pattern).
The shipped per-word loop is preserved verbatim as the fallback; the
new code is purely additive:

```c
static int pb_run_has_fence(uint32_t method, int noninc, uint32_t n)
{
    if (noninc)
        return method == 0x1D70u;
    return method <= 0x1D70u && 0x1D70u < method + 4u * n;
}
```

In the DEKO3D bulk block: `if (fencescan_on() && !s_scan_enabled)
has_fence = pb_run_has_fence(method, noninc, n); else <shipped loop>`.
The loop is kept whenever `s_scan_enabled` because it also feeds
note()'s inventory (§1.3's condition). The per-word fallback for
genuine fences (which assigns `g_pb_cmd_va` at scan.c's fallback loop,
not in the pre-scan) is untouched.

**Gate — predicate-vs-loop equivalence: PASS.** Host harness
(`~/jsrf-dpack-scratch/fencetest.c` on the LOQ) with the predicate
extracted verbatim from the patched scan.c (`predicate.inc`), against
a reference implementing the shipped loop's semantics: every
4-aligned method 0x0000–0x1FFC × noninc {0,1} × n ∈
{1,2,3,4,5,17,64,511,2047} = 36,864 checks, plus 11 named shapes
(fence alone at n=1 both kinds, at head/middle/tail, just-missed
below/above, the long transform-range run crossing 0x1D70, long
noninc fence and non-fence runs). **36,875 checks, 0 mismatches.**

## 2. B.3 — data-method shadow skip (commit 0013dfe, exec_dk.c +35/−4)

Toggle `RECOMP_PB_DATASHADOW`. Exactly per §1.2 + §1.1's parity
condition:

- The three `shadow_run(method, p, n)` calls in `nv2a_pb_exec_run`
  (AE16 / AE32 / INLINE_ARRAY cases) are gated on a hoisted
  `const int datashadow` local: `if (!datashadow) shadow_run(...)`.
- `nv2a_pb_exec_method`'s prologue (the subch==0 `shadow_write`
  before the s_plain shortcut) carves out the same three methods
  (`NV097_ARRAY_ELEMENT16`, `0x1808`, `NV097_INLINE_ARRAY`), so with
  the toggle on, RECOMP_PB_FAST=0 maintains the three words exactly
  as much as the fast path does — never.
- The getter's comment carries the contract caveat from §1.1:
  `regs[m/4]` stops being the title's last written value for three
  write-only data ports; that is the whole exposure.

**Gate — audit + compile-level caller census: PASS.** The §1.1
reader audit is the replay-relevant reasoning (no consumer reads
words 0x600/0x602/0x606 through any alias; block 0x60's transport is
dirtied by DRAW_ARRAYS' own prologue write in the patrol pattern).
Re-run on this tree post-edit, the census is: `shadow_run` exists
only as its definition plus the three gated calls, tree-wide; the
remaining `shadow_write` call sites are the semaphore block (methods
0x01A4/0x1D6C/0x1D70 only), the carved-out prologue,
`vp_upload_run` (transform program/constant methods only), and
exec_run's learned-plain loop (data methods have explicit
exec_method cases and never become plain — the learning site is the
default case). `s_reg` is assigned only inside `shadow_write` /
`shadow_run`. No maintenance point for the three words escapes the
toggle.

## 3. A.5 — free trims (commit 73f6d4e, queue.c −2, exec_dk.c +23/−1)

No toggle, per spec — its own commit so the campaign-base build is
its control arm.

- **queue.c:** `p_attr_const` re-verified write-only on this tree
  before deleting (full-tree grep: declaration at queue.c:156 and
  the single write in q_draw's send_attr block; no reads). Both
  lines deleted; post-edit grep confirms zero references remain.
  The generation bookkeeping (`p_attr_gen`, `p_attr_ok`) is
  untouched, so `send_attr` fires exactly as before.
- **exec_dk.c `describe_state`:** the unconditional 256 B
  `attr_const` memcpy is now made only when
  `s_gpu.attr_const_gen` differs from the last described generation
  (file-static tracking in the function). Caller verification, as
  §2.4 instructs: `describe_state`'s only caller is `raw_batch`
  (exec_dk.c:1291); its consumers are (a) the queue — `q_draw`
  reads `b->attr_const` only when the generation differs from its
  own `p_attr_gen` (queue.c's `send_attr`), and the queue thread
  rebuilds every batch's `attr_const` from its `c_attr_const`
  mirror, so on an unchanged generation the descriptor bytes are
  never read; (b) the DK back end's per-batch reads
  (`vd.attr` copy, `av_vertex`, the cap_draw diagnostic) — reached
  through the queue's rebuilt batch in the shipped configuration,
  but direct with `RECOMP_DK_THREAD=0`. **Deviation from the study's
  letter, taken on the verification it asked for:** the gate is
  additionally conditioned on `nv2a_backend_queue_on()` and on no
  raw-batch hook being installed, so in the direct-backend and
  hook cases the copy stays unconditional and shipped behaviour is
  preserved byte-for-byte; the trim applies on the shipped queue
  path only. (This is the staleness hazard Surgery 1's report
  flagged for this same copy, answered by construction instead of
  by assumption; no hook setter exists in this tree.)

## 4. NOTE_TRIM (commit 8d93d0c, exec_dk.c +42/−4)

Ported from Surgery 1's patch (`~/jsrf-nx-surgery1` @ 0c08e2f) —
its NOTE half only; STATE_TRIM is not in this pack. Content
identical in substance to the surgery patch: `note_trim_on()`
getter; `note_unhandled` fast head (one `unhandled_total++` and
return); exec_run plain-loop batching (`trim_noted` folded into the
total once per run) with both `rest_note` call sites skipped under
the toggle; the report's extra line under the toggle ("per-method
unhandled ranking off …: the total above is exact"). The [GPU]
total stays exact; the disclosed loss is the per-method ranking
and distinct count.

**Gate — Surgery 1's harness method, re-run fresh on this base:
PASS.** Surgery 1's shipped harness (captest.c) targets
capture_render_state / STATE_TRIM, which this pack deliberately
does not port, so the NOTE gate was rebuilt the same way rather
than inherited: the real patched `note_unhandled` (plus the real
`note_trim_on`) extracted from the dpack source, against the
pristine `note_unhandled` extracted from the campaign-base commit,
each side with its own globals, driven over all 2048 methods × 5
params plus a 512-method second pass for the slot-hit path —
10,752 calls per side, at the source's real
`PB_EXEC_MAX_UNHANDLED` (2048) and struct layout, with
`unhandled_on()` = 1 (the patrol configuration, where noting is
on; the total's only reader is the report, which is skipped when
quiet). Toggle OFF: total, distinct count, full per-method table
and slot table **byte-identical**. Toggle ON: total **exact**
(10,752 = 10,752), per-method table never touched (distinct 0).
Harness: `~/jsrf-dpack-scratch/noteharness/` on the LOQ.

## 5. Patrol A/B arm spec (hardware phase, not run here)

Uniform conditions (study §3): the campaign patrol (99th Street
entry-plaza, blueprint replay, FWD 14 s / BACK 28 s / FWD 14 s ×4,
Game Data 1), pooled baseline **33.0 ms**; **[perf] walk µs/draw is
the primary instrument**, frame buckets (combined 400–699 median +
sub-buckets) second; every run passes the stock-clock gate from its
own [SWPERF] samples (uniformly 1020/384/1331); shader pack frozen;
options otherwise the campaign baseline. Toggles are options.txt
keys, absent = OFF.

| Arm | Build | Toggle(s) | Extra gate |
|---|---|---|---|
| BASE | campaign-base build of `99c15fb` (own WORK_DIR build — not produced by this desk phase) | — | A.5's control; also the OFF-state reference |
| B.1 | dpack NRO `aff65eae…` | `RECOMP_PB_FENCESCAN=1` | scan share leaves the dwell |
| B.3 | dpack NRO | `RECOMP_PB_DATASHADOW=1` | **replay bit-exactness: the blueprint replay must be frame-exact; any divergence kills the item** (products untouched by construction); shadow_run's share leaves the dwell |
| NOTE | dpack NRO | `RECOMP_DK_NOTE_TRIM=1` | [GPU] unhandled total identical to the BASE/OFF arm's |
| A.5-pack | dpack NRO | none (A.5 is compiled in) | judged against the BASE arm, walk µs/draw only (expected ~0.1 ms class) |
| ALL-ON | dpack NRO | all three =1 | replay bit-exactness as B.3; the shipping candidate if the singles price out |

Study expectations, for the record: B.1 realistic 0.1–0.3 ms, B.3
0.3–0.4 ms, NOTE ceiling ~1.0 ms (Surgery 1), A.5 ~0.1 ms. At the
plaza these are sub-quantum at the median bucket; walk µs/draw
carries the verdicts, exactly as Surgery 1's proof plan framed it.

## 6. What was NOT done / not verified

- No console contact, no boot, no push — desk only, per commission.
- The campaign-base (`99c15fb`) NRO was not built; the BASE arm
  needs it built before the A/B (one build command, same recipe,
  own WORK_DIR).
- describe_state's gate rests on the queue-path consumer analysis
  above; the direct-backend path is preserved by construction, not
  by a runtime test (it cannot be exercised without a console).
- B.2 (NEON AE16), B.4, A.3 are not in this pack (study §3 order:
  they follow in their own commissions).
