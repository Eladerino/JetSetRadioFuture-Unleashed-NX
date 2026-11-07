# ZC-FIX step 1 — zero-copy consumer-state telemetry build

Desk only: no console contact, nothing pushed, **no fix attempted**.
This is the instrument DECODE-DATA-STUDY §2.5 requirement 1 demands
before any fix attempt on the COPY_VERTICES=0 hang: *"add
consumer-state telemetry (current record type + time inside the inner
call, printed in the same [perf] block). One reproducing boot then
names its wait instead of implying it."*

## Tree and commits (LOQ, `~/jsrf-nx-zctelem`)

Forked from `~/jsrf-nx-rc042` @ 53a83f2. Two local commits, neither
pushed:

| Commit | What |
|---|---|
| `94a3f10` | Campaign base: `menufix.patch` applied (uam compile serialisation). Verified on apply: `git diff --stat` = **one file, +23** (`src/third_party/uam/source/uam_api.cpp`), exactly as specified. |
| `3dc818e` | The instrument: `src/kernel/nv2a_backend_queue.c`, **+136/−1**, one file. |

Post-build tree re-verified (LOQ storage-rollback guard): clean
status, exactly these two commits on 53a83f2.

## Build

`WORK_DIR=$HOME/jsrf-switch-build-zctelem JSRF_GEN_OPT=-O3
SKIP_RECOMP=1 tools/switch/build.sh "<ISO>"` — exit 0. History,
plainly: the first build session died at ~95% (the LOQ's known
session-death; log preserved at LOQ `~/zc-build.log`), and the
resumed build (`~/zc-build2.log`) completed incrementally. The
instrument's object provenance was checked rather than assumed:
`nv2a_backend_queue.o` was compiled 11:53 from the instrumented
source (11:52) and carries the telemetry strings; the linked NRO
carries them too, and its version string reads
`0.4.1-switch-7-g3dc818e`.

- **NRO (LOQ):** `~/jsrf-switch-build-zctelem/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
- **Size:** 44,910,789 bytes (same size as the RC/menufix NROs; the
  change is telemetry in an existing translation unit)
- **sha256:** `b57905170e93d112100612b247429a2be4efcb7467977ead858483e5b70354e9`

## Instrument design

**Gate.** `RECOMP_QUEUE_ZC_TELEM=1`, read once at queue bring-up via
the file's own `env_num(getenv)` pattern — the same mechanism as
every other `RECOMP_QUEUE_*` knob in this file (options.txt lines
reach this code as environment variables; `COPY_VERTICES` itself is
read exactly this way). Absent ⇒ `s_zc_telem` stays 0 ⇒ every hook
is skipped: **no timestamps are taken, no counters are touched, and
no code path changes length in any way that can affect ordering,
gating or delivery** — in either mode. The instrument observes; it
cannot participate.

**Hooks (all in `nv2a_backend_queue.c`):**

1. **Consumer state (requirement (i)).** In `q_thread`'s record
   loop, immediately *before* `q_run(...)` the consumer publishes
   the record type (`zc_cur_type`), the record's ring position
   (`zc_cur_tail`) and the entry time (`zc_cur_since = q_ns()`),
   with the type/tail relaxed-stored before the timestamp's
   SEQ_CST store, so a reader never sees a fresh timestamp beside
   a stale type. Published *before* the call on purpose: if the
   call never returns, the report still names it. After `q_run`
   returns, the consumer accumulates per-class totals
   (`zc_made_n[6]` / `zc_made_ns[6]`, indexed by record type) and
   resets the current-record fields to idle. Timestamps exist only
   at these record boundaries — two `q_ns()` calls per record,
   gate on. The big-record path (`q_draw` making a record on the
   executor after `prod_wait(W_ALL)`) deliberately does **not**
   touch this state: it is not the consumer, and §2.5 candidate
   (iii) is read off the existing counters instead (below).
2. **Ring position vs `p_need` (requirement (ii)).** The report
   loads the consumer's `s_tail` and the handed-over `s_head`
   atomically and reads this thread-family's `p_need` (the executor
   position after the last touching record — the exact value
   `now_ok()`/`queue_sync` gate delivery on), printing all three
   plus the unmade bytes ahead of need.
3. **Store-wait split (requirement (iii)).** When
   `nv2a_backend_queue_store` takes the *queued* branch (the only
   branch that waits), and only under the gate, the executor walks
   the unmade span — headers from the consumer's tail snapshot to
   its own committed `p_head`, i.e. precisely the records this
   store will be delivered behind, in ring order — and charges the
   store to the **first** unmade record's class
   (`zc_behind[3]`: draw = Q_DRAW/Q_CLEAR, flip = Q_FLIP,
   other = Q_STORE/Q_CALL). The same walk sets `zc_flip_ahead`
   when any unmade Q_FLIP sits anywhere ahead of the store — the
   candidate-(i) fingerprint. The walk reads only headers the
   executor itself wrote, is bounded (guard counter, size sanity),
   and charges "other" rather than walking a garbled span. Cost:
   queued stores only, gate on only; the span is bounded in
   practice by the batching/flush discipline. Stores made at once
   (`now_ok()`) need no split — the existing
   `st_stores_now` already counts them, and they wait behind
   nothing by construction.

**Deliberately not instrumented** (readings that would have needed
a behaviour change, per the commission's rule): `queue_call` gets
no parallel split (the study's counters and this requirement name
stores; calls share the delivery gate and their waits are visible
in the consumer-state fields); no sub-phases inside `q_run` (e.g.
state-apply vs `draw_raw`) — naming the *record type* is what
§2.5 asks, and finer splits would mean instrumenting the back
end's call sites, not the queue's boundaries.

## The exact log line added

One line appended to the existing queue `[perf]` line, in the same
report call and cadence (the report function's other counters reset
exactly as before; the zc counters reset with them):

```
[perf] zc-telem: consumer <cur>; ring tail <tail>, head <head>, need <need> (<kb> KB unmade ahead of need); made/frame: draw <n> (<ms> ms), clear <n> (<ms> ms), flip <n> (<ms> ms), store <n>, call <n>; stores queued behind draw <n>/frame, flip <n>, other <n>; with a flip ahead <n>
```

where `<cur>` is either `idle (between records)` or:

```
making <draw|clear|flip|store|call>, <t> ms inside the call (record at ring <pos>)
```

Field guide: **consumer/making** = requirement (i) — the record
type the consumer thread is inside *right now* and how long it has
been inside; in a stall this number grows by the [perf] block
interval every block while everything else freezes. **tail/head/
need** = requirement (ii) — tail is what the consumer has made,
head what the executor has handed over, need what delivery of
progress stores is gated on; `need − tail` is the backlog a queued
store must outlive. **made/frame** = per-class consumer throughput
and time over the block (a class can be the bottleneck in
aggregate even when no single call hangs). **stores queued
behind …** = requirement (iii) — the split of the existing
`st_stores_queued` by first unmade record class, plus the
flip-ahead count.

## The reproducing boot (spec for the future console run)

Run exactly as proof-window Leg 1 ran its A.1 arm, with two
options lines and this NRO as the only deltas:

1. Console claim protocol + capture-verified foreground, as
   always. Stage this NRO to `switch/jsrf-unleashed/jsrf-unleashed.nro`
   (hash-verify on the card against `b5790517…`).
2. options.txt: take the Leg 1 arm's options state and set
   **both** lines present: `RECOMP_QUEUE_COPY_VERTICES=0` (the
   mode under test, as Leg 1) and `RECOMP_QUEUE_ZC_TELEM=1` (the
   instrument). Nothing else changes.
3. Launch via the forwarder. Boot banner must show the queue line
   with `RECOMP_QUEUE_COPY_VERTICES=0` (mode engaged, as Leg 1's
   log :29) — the zc line itself appears only in [perf] blocks.
4. Let it run into the stall (Leg 1: 9 draws made, then black,
   executor asleep). **Hold the stall for at least ~60 s** so
   several [perf] blocks print inside it; the inside-call time on
   the zc line must be seen to *grow* across at least two blocks.
5. Close via HOME → X → A, then pull `Logs/last-run.log` before
   any relaunch (the running game holds it locked; a relaunch
   rotates it). Restore options.txt byte-exact afterwards, as
   Leg 1 did.

**What the zc line shows for each of §2.5's three candidates:**

- **(i) The flip-overtaking cycle (prime suspect).** The last
  live blocks read `consumer making flip, <t> ms inside the
  call` with `<t>` growing block over block into the seconds;
  `tail` parked at the flip record while `need` sits beyond it
  (`KB unmade ahead of need` > 0); made/frame shows draws/clears
  completing in earlier blocks but flip ≈ 0 in the stall blocks;
  and in the blocks leading in, `stores queued behind flip` > 0
  and/or `with a flip ahead` > 0. That combination names the
  wait exactly: a progress store queued behind a flip whose
  present path is waiting on the guest that is waiting on the
  store.
- **(ii) Trap-sync / software-method interlock.** The consumer
  is exonerated in the print: `consumer idle (between records)`
  (or cycling normally), `tail` at or past `need` (`0 KB unmade
  ahead of need`), made/frame continuing across blocks, the
  behind-split ≈ 0 — while the *existing* line's trap counters
  (traps/frame, trap ms) carry the stall. The cycle then lives
  between the executor and the guest, not inside a consumer
  call, and the fix conversation moves to `queue_sync`'s wait
  set (§2.5 requirement 3) with the consumer cleared.
- **(iii) The big-record path.** Consumer again idle with
  tail == head == need, but the existing line's `made on the
  executor` count and its "until idle" (W_ALL) wait figures
  spike in the blocks before the stall: the executor was making
  oversized records itself after waiting for the queue to drain,
  and the interlock sits in that hand-off on the boot path —
  the least consistent candidate per the study, now directly
  checkable instead of argued.

A healthy control is available for contrast in the same log: the
boot's early blocks (and any COPY_VERTICES=1 boot with the gate
on) show the consumer cycling, small inside-call times, and the
behind-split dominated by draw.

## Not done, by design

No fix, no ordering/gating/delivery change, no soak, no console
run. The boot above is step 2 of the §2.5 programme; it runs when
the campaign commissions it.
