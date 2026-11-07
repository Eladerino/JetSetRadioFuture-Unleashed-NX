# Campaign S-0 — counters build (desk complete, Nov 6, 2026)

Act 1 of the renewed perf campaign (STATE-STUDY.md §3 S-0). Desk only:
no console contact, nothing pushed. The build carries counters and
nothing else — with the stats gate absent/off, behaviour is the
campaign base byte-for-byte in every value, dirty mark and generation
bump (the counters only read).

## Tree (LOQ ~/jsrf-nx-campaign)

| Commit | What |
|---|---|
| `0606cf338ca09d8f73a1ae64dbea2f5f182fde95` | S-0 counters (this build; droppable without touching the base) |
| `d6edf19b98470fbc82ccf503c7688e3f59fa9751` | **Campaign base**: uam compile-funnel mutex (menufix.patch applied to rc042; `git diff --stat` verified exactly one file, `src/third_party/uam/source/uam_api.cpp`, +23) |
| `53a83f2` | RC tip (the 0.4.2 candidate tree) |

Post-build re-verification (LOQ storage-rollback guard): the tree
still carries exactly these two commits on 53a83f2, working tree
clean, and the NRO hash below was re-read from disk after the build.

## Build

- Command (verbatim, held session):
  `cd ~/jsrf-nx-campaign && WORK_DIR=$HOME/jsrf-switch-build-campaign-s0 JSRF_GEN_OPT=-O3 SKIP_RECOMP=1 tools/switch/build.sh "<ISO>"`
- Exit 0, 100%.
- NRO: `~/jsrf-switch-build-campaign-s0/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
  (on the LOQ; build.sh also staged its usual copy to
  `Downloads/jsrf-unleashed-switch`)
- Size **44,914,885 B**, sha256
  `9ea0b4d3a9cf15a770c2932c113ead18a06fd1797cc906ae115468b945436afb`
- Version string in the NRO: `0.4.1-switch-7-g0606cf3`
- Both `[stats]` format strings confirmed present in the NRO.

## What happened to the first attempt (the 43-minute silence)

Fact, from disk state at the recovery check (12:34 NST): the campaign
base commit existed and was correct; **the counters patch had never
been applied** (zero `RECOMP_DK_STATS` markers in all four files),
there was no build directory, and no build was running. The patch
delivery session had hung with zero output for ~43 minutes and was
killed. Two causes, both evidenced:

1. A local helper script in the shared scratch dir was
   **overwritten one minute into the attempt** (mtime 11:50 NST) by
   another process — its contents were replaced with a different ssh
   wrapper, so the in-flight command no longer did what it was
   written to do and the session sat in an interactive Windows cmd
   prompt consuming the script as cmd input.
2. The delivery also double-base64-encoded the patch, and on retry
   the resulting `wsl bash -c` line exceeded the Windows command-line
   length limit outright (`The command line is too long.`).

WSL had additionally restarted during the contention window (uptime
11 minutes at the check; three sibling -j4 builds had OOM-died in the
15 GB WSL earlier, per the parent). Recovery used a uniquely-named
helper and a gzipped patch transfer (~5 KB encoded); the patch then
applied cleanly on the first try (all 14 anchors exact), and the
build above succeeded on its first run in a held session with the
LOQ otherwise idle. No verified work was redone: the base commit was
kept as found.

## Counter design (commit 0606cf3, 4 files, +184/−4)

Gate: **`RECOMP_DK_STATS`** — set in options.txt either as the raw
variable (`RECOMP_DK_STATS=1`) or by its new friendly name
(**`stats=1`**, added to the k_opts table in
`port/switch/jsrf_switch.c`). Read once per process, cached; absent
or `=0` ⇒ every counting site is a predicted-false branch and the
[stats] lines never print.

1. **vp no-op counters** (`src/kernel/nv2a_pb_exec_dk.c`,
   `s_vpstats`, exposed cumulatively via `nv2a_pb_exec_vpstats()` in
   the `s_ws` style): for each upload word, whether its param equals
   the destination slot's current value, split **prog / const /
   xfctx**, each with its total-words counter. Counted at **all three
   entry points**: `vp_upload` (the one-word path), `vp_xfctx`, and
   **`vp_upload_run`** — the run path carries the bulk of the ~45K
   words/frame, so counting only the brief's two named functions
   would have missed the stream; the split and semantics are the
   brief's. Totals count every word processed per class (including
   out-of-range words that land nowhere and still bump generations);
   a no-op is an in-range word equal to the slot's current value.
   Count-only: no store, dirty mark, or generation bump is altered.
2. **rows_diff counters** (`src/kernel/nv2a_backend_queue.c`):
   invocations, rows scanned (hi−lo summed), and differing rows
   shipped, split prog/const via a new `kind` parameter on
   `rows_diff` (the only signature change; both call sites updated).
3. **Per-record dirty-block histogram + queue remainder split**
   (same file, counted at `state_write` entry, where `p_nblk` /
   `p_pr` / `p_cr` are intact): histogram of dirty blocks per state
   record (bins 0,1,2,3,4,5–8,9–16,17–32,33–64,65+), mean
   blocks/record, and bytes split as shadow blocks
   (`nblk × sizeof(QBlock)`, 80 B/block incl. its index word) vs
   prog runs vs const runs (run headers + row payloads). The
   "fixed per-record structs" share is reported as the queue total
   minus everything accounted (indices, converted attrs, vertices,
   shadow, prog, const) — i.e. headers, QDraw/QClear/batch structs,
   alignment padding and store/call records. This is SURGERY-DESIGN
   A.4's split, in the same run.
4. **Printing**, on the existing [perf] cadence (the ~2 s
   `nv2a_dk_perf` report; counters reset with theirs):
   - From `nv2a_dk_perf` (`src/nv2a_dk/nv2a_dk.c`):
     `[stats] vp uploads: prog <w> words/frame, <n> no-op (<p>%); const <w> words/frame, <n> no-op (<p>%); xfctx <w> words/frame, <n> no-op (<p>%)`
   - From `nv2a_backend_queue_report` (rides the queue's [perf] line):
     `[stats] queue: rows_diff prog <calls> calls, <scanned> rows scanned, <rows> rows shipped; const <calls> calls, <scanned> rows scanned, <rows> rows shipped; state records <r>/frame, dirty blocks/record mean <m>, hist 0:.. 1:.. 2:.. 3:.. 4:.. 5-8:.. 9-16:.. 17-32:.. 33-64:.. 65+:..; remainder <t> KB/frame (shadow <s>, prog runs <p>, const runs <c>, fixed structs/headers/padding <f>)`
   - The REST census needs no new code (in-tree, `RECOMP_PB_REST=1`);
     its output is the `; the rest by method: XXXX n% …` tail of the
     existing `[perf] pushbuffer:` line.

## Console run spec (exact; for the console coordinator)

Common: claim the console per protocol + capture-verified foreground
before first input. Stage the S-0 NRO above (hash-verify on the
card). **Stock-clock gate: the run counts only if its own [SWPERF]
samples are uniformly 1020/384/1331.** Shader pack must be present
and frozen (do NOT delete `dk_shaders.pak`; a pack-rebuilding boot is
a different experiment). Blueprint replay patrol at the 99th Street
entry plaza facing the big tower (blueprint recording
`pad-record-blueprint-20261105-1237.bin`, FWD 14 s / BACK 28 s /
FWD 14 s × 4, Game Data 1), frame-exact replay as the correctness
gate. Baseline for context: pooled 33.0 ms; these runs are read for
**shares and rates, not frame verdicts** (counters + sampler perturb;
shares are what transfer).

### Run 1 — counters dwell (prices S-1, refines S-3/S-5, splits A.4)

options.txt: take the card's resting options and **add two lines**:

```
stats=1
sample=1
```

(leave everything else exactly as found). Boot, load Game Data 1,
run the patrol replay twice back to back, pull the full log before
any relaunch. Read, averaged over the patrol's steady windows:

- `[stats] vp uploads:` → the prog/const/xfctx no-op rates.
- `[stats] queue:` → rows_diff calls + rows scanned per class;
  blocks/record mean + histogram; remainder split KB/frame.
- `[SWPROF]` shares from the same run (the sample=1 dwell re-run the
  study's §1.3 specifies).

### Run 2 — REST census (prices S-4)

Fresh boot. options.txt: resting options **plus one line** (no
stats, no sample — the census's own ~7% walk cost is the known
perturbation and it should ride alone):

```
RECOMP_PB_REST=1
```

One patrol replay, pull the log. Read the `the rest by method:`
percentages on the `[perf] pushbuffer:` lines: composite-matrix
method range (0x0C00-ish SET_COMPOSITE_MATRIX words as printed) plus
viewport-offset words, as a share of total words.

## What each number decides

- **S-1 go/no-go and its price** (Run 1 vp lines): the combined
  prog+const no-op rate IS S-1's price (STATE-STUDY §2.2/§3 S-1).
  ~25% ⇒ ~0.15 ms — defer; S-1 is not the next build. ~75% ⇒
  0.2–0.6 ms, the largest single state item — build S-1 next.
  Between: interpolate honestly (~0.15 ms per 25 points is the
  study's own arithmetic). The xfctx rate separately prices how much
  of the constant range-widening (rows 56–59) a gate would remove.
  The `[stats] queue` rows_diff calls/scanned are S-1's compound
  term: they show how much 136-row scan work per window the gate
  would actually skip (a no-op-only upload burst currently still
  triggers the full scan).
- **S-3 refinement** (Run 1 histogram): regs_collect scans 128 flags
  per record regardless; the histogram's mean blocks/record is the
  sparsity S-3's dirty-list would exploit. Low mean (single digits)
  ⇒ the scan is mostly empty work and S-3's 0.1–0.25 ms estimate
  firms up; a fat right tail ⇒ the scan is earning its keep and S-3
  drops in priority.
- **S-5 / A.4 branch verdict** (Run 1 remainder split): shadow KB
  dominating the ~1.1 MB remainder ⇒ shadow-traffic attacks (and
  B.3's context) stay live; prog/const runs dominating ⇒ the vp
  shipment path (S-1's downstream, mirror writes) is the branch;
  fixed structs/headers/padding dominating ⇒ A.4 closes as
  fixed-cost noise and nobody quotes a remainder saving again.
- **S-4 go/no-go** (Run 2): composite + viewport words **≥ ~0.75%**
  of the stream ⇒ schedule the run-level transform capture;
  below ⇒ S-4 is not built (the in-tree prior says no rest method
  leads; this confirms it for the tower patrol specifically).

## Not done / not claimed

- No host harness: the counters have no shipped-value semantics to
  harness against (count-only, gate-off = untouched paths); the desk
  verification is the anchor-exact patch, the full Switch build, the
  in-NRO strings, and the post-build tree/hash re-verification
  above. The first console run is their first live exercise — if a
  [stats] line is absent or malformed there, that is a finding
  about this build, to be reported as-is.
- Surgery 1's toggles remain absent from this tree (as in rc042);
  nothing in S-0 depends on them.
