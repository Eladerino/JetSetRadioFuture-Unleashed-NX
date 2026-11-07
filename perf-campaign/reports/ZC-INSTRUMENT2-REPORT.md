# ZC-FIX step 2 — executor-side write/need accounting instrument

Desk only: no console contact, nothing pushed, **no fix attempted**.
This is the instrument WINDOW1-REPORT's ZC section commissioned:
*"Why the executor advanced `p_need` across a span it never committed
is the one link this instrument cannot see (executor-side write/need
accounting was not instrumented) — that is the ZC-FIX programme's
next instrument."* Step 1 (ZC-TELEM-REPORT) watched the consumer and
the three ring positions. Step 2 watches the only thread that writes
the accounting: the executor.

## Tree and commits (LOQ, `~/jsrf-nx-zcinstr2`)

Forked from `~/jsrf-nx-zctelem` @ `3dc818e` (which is campaign base
`94a3f10` = rc042 `53a83f2` + menufix, plus the step-1 instrument).
One new local commit, not pushed:

| Commit | What |
|---|---|
| `a8b6164` | The instrument: `src/kernel/nv2a_backend_queue.c`, **+266/−14**, one file. |

The patched file's sha256 was verified identical between the
authoring copy and the LOQ tree before commit
(`63197d9c…`); post-build tree state re-verified (see Build).

## What the readings had to explain

Window 1's stall, from the first [perf] block onward: `tail 51104,
head 51104, need 56016` — `p_need` 4,912 B past the handed-over head,
consumer idle, executor asleep, and nothing ever satisfied the need.
Step 1 could not say *which* records made up the 4,912 B span, *who*
set the need there, *when*, or whether any hand-over ever covered
part of it — because all of that is decided on the executor side,
in exactly one place each:

- `p_need` is written in **exactly one statement** in the tree:
  `q_commit()` — `if (touches) p_need = p_head;` after
  `p_head += p_wrap + size`. Need is therefore, by construction,
  "the end of the most recent touching record". Its inputs are
  that record's own size and wrap pad, and its `touches` flag,
  which the five call sites fix as: draw/clear →
  `!s_copy_vertices` (i.e. always 1 in the ZC mode under test),
  queued store → 1, queued call → 1, flip → 0.
- `s_head` (the handed-over span) is published in exactly one
  place: `q_flush()` (renamed `q_flush_r` here, trigger-tagged),
  called from five triggers: the batch threshold inside `q_commit`
  (16 records or 16 KB), a flip, `prod_wait` before any wait,
  `nv2a_backend_queue_sync`, and the public flush API.
- A store or call is delivered at once iff `now_ok()`
  (`tail >= p_need`) at its decision moment; otherwise it is
  committed as a record behind the gate.

So the whole question collapses to an event history: commits,
hand-overs, and gate decisions, in order, with positions. That is
what this instrument records.

## Instrument design

**Gate.** `RECOMP_QUEUE_ZC_TELEM=2` (the `env_num` clamp in queue
bring-up now allows 0–2). Level 1 keeps step 1 *exactly* — every
step-1 hook tests `s_zc_telem != 0` and is untouched; every step-2
hook tests `zc2_on()` (`s_zc_telem >= 2`). Flag absent ⇒ level 0 ⇒
no timestamps, no event writes, one cached-int test per call site:
the shipped path's behaviour is unchanged, exactly as step 1
guaranteed. The mode under test still needs
`RECOMP_QUEUE_COPY_VERTICES=0`, set separately, as before.

**The event ring.** A static ring of 128 events (`Zc2Ev`, 40 B
each, ~5 KB of BSS), written **only by the executor thread** —
commits, flushes, and syncs cannot originate anywhere else, and
the store/call decisions are executor-side by definition. The
[perf] report runs on the same thread (it already reads the
executor-local `p_need` directly), so the ring needs no atomics.
Each event carries: `q_ns()` timestamp, a global sequence number,
a kind, a start/end pair of stream positions, a size, an aux word,
a record type and flags — all count/offset data, no payloads.

**The five event kinds:**

| Kind | Written at | start / end | size | aux | flags |
|---|---|---|---|---|---|
| `C` commit | `q_commit()`, every record | record data start / `p_head` after | record size | draw: vertex_count; clear: flags; store: word addr (low 32); call: fn (low 32) | bit0 `T` = touches (this commit set `p_need` to `end`); bit1 `W` = a ring wrap pad precedes the record |
| `F` flush | `q_flush_r()` when it actually publishes (`s_head != p_head`) | `s_head` before / after | bytes handed over | — | the trigger letter: `b` batch, `f` flip, `w` producer wait, `s` sync, `a` flush API |
| `S` store | `nv2a_backend_queue_store()`, both branches, at decision time | gate `p_need` / `s_head` then | the value written | word address (low 32) | 1 = queued behind the gate, 0 = made at once |
| `A` call | `nv2a_backend_queue_call()`, both branches | gate `p_need` / `s_head` then | — | fn (low 32) | 1 = queued, 0 = ran at once |
| `Y` sync | `nv2a_backend_queue_sync()`, after its own flush | gate `p_need` / `s_head` then | — | — | bit0 = `all`; bit1 = the wait blocks (`prod_ready` false at entry) |

Wrap handling: a wrapping record's `Q_WRAP` header is written by
`q_reserve()` ahead of the record; the commit event's `start` is
the record's *data* position (stream coordinates, wrap pad
excluded) and the `W` flag marks the wrap, so the pad is
recoverable as `start − (previous event's end)`.

Alongside the ring, the instrument keeps `zc2_need_ev` — the
sequence number of the commit that most recently set `p_need` —
updated in `q_commit` under the gate, and per-[perf]-block event
counters (commits, flushes, stores queued/at-once, calls
queued/at-once, syncs), reset by the report like step 1's.

## The exact log lines added

Step 1's `[perf] zc-telem:` line is unchanged. Immediately after
it, at level 2 only, one more line in the same report call and
cadence:

```
[perf] zc-acct: phead <ph>, need <n> -- <N> B past head, unhanded; need set by C#<seq> <type> @<start>+<size>, <age> ms ago; acct/frame: commits <c>, flushes <f>, stores q <q>/now <w>, calls q <q>/now <w>, syncs <y>; last events: <ev>; <ev>; …
```

- If `p_need <= s_head`, the second field reads
  `need <n> -- within head;` instead.
- If the need-setting commit has rotated out of the 128-event
  ring, the clause reads `need set by C#<seq> (rotated out of the
  event ring);`. If no touching record has committed yet, the
  clause is absent.
- `phead` is the executor's committed end (`p_head`) — the fourth
  position step 1 never printed. The four positions together
  separate the four states a span can be in: written/accounted
  (≤ `phead`), handed over (≤ `head`), made (≤ `tail`), and
  demanded (≤ `need`).

**Event format** (newest 24, oldest first, `;`-separated;
`#<seq>@<ms>` = sequence and milliseconds since the first event
of the boot):

```
C:  #12@3401 C draw @51072+4912 T v812
    #13@3401 C store @56000+32 T
F:  #14@3402 F b 51104->56016
S:  #9@3398 S w1d70=00000001 queued gate 56016 head 51104 OUT
A:  #7@3390 A fn83a42c10 now gate 49120 head 49120 IN
Y:  #20@3500 Y all=0 need 56016 head 51104 blocks
```

Field meanings: for `C`, `@<start>+<size>` is the record's span in
stream coordinates, `T` marks a need-setting commit, `W` a wrap,
`v<n>` a draw's vertex count. For `F`, the letter is the trigger
(table above) and the pair is the hand-over's from→to. For `S`/`A`,
`gate`/`head` are the decision-time positions and `OUT`/`IN`
decodes the one comparison that decides deliverability:
`OUT` = the gate stood past the handed-over head at decision
time, so the record entered the ring behind an unhanded span;
`IN` = the gate was already handed over (queued only because the
consumer had not *made* it yet — the benign case).

## What each question gets from one reproducing boot

**(a) How `need` is computed, per block.** The block line names
the standing need's setter directly: `need set by C#<seq> <type>
@<start>+<size>, <age> ms ago`. Since need ≡ that commit's `end`
by construction, the print plus the setter's own `C` event in the
dump is the complete computation: previous `p_head` (= the
preceding event's end) + wrap pad (the `W` flag / start delta) +
size. There is no other input — a grep of the tree finds no other
`p_need` write.

**(b) Every advance of the committed span, timestamped against
need's changes.** Committed span has two ends and both are
events: `C` events advance `p_head` (each carrying its own time
and whether it also moved `need`), `F` events advance `s_head`
(each carrying its trigger). Interleaved in one sequence, they
show — for the standing need's setter — whether any `F` event
ever reached its `end`, and if not, which trigger *should* have
fired (e.g. a batch-threshold `F b` is owed at 16 records/16 KB
past the last hand-over; its absence is itself a reading).

**(c) Every progress-store/flush event, inside or outside the
committed span.** Guest progress stores surface as
`nv2a_backend_queue_store` — one `S` event per call, decision and
positions attached, `OUT`/`IN` decoded. Calls likewise (`A`).
Flushes in both senses are covered: queue hand-overs are `F`
events, and guest-forced synchronisation points
(`nv2a_backend_queue_sync`, the NOP-trap path) are `Y` events
with their blocking state. A store whose `S` reads `queued …
OUT` and whose matching `C store` event then sits in the dump
with no later `F` reaching its end is, byte for byte, the
uncommitted-span link Window 1 could not prove.

**(d) The identity of the first unsatisfiable need.** The first
need that can never be satisfied is the first `T`-flagged commit
whose `end` no subsequent `F` ever reaches while the executor
then goes quiet. In the stall, every block repeats the same
frozen event tail and the summary's `need set by` age grows in
real time — the setter event *is* the identity: its type, span,
and (for a draw) vertex count; for a store-setter, its own `S`
event one or two positions earlier carries the word address and
value. If the setter is a draw, the span arithmetic across the
dump's `C` events shows which records compose the 4,912 B.

## What would prove / refute the (ii)-family placement

The hypothesis (Window 1): *a progress store was queued early
behind that `need`; in read-later mode its delivery is gated on
the consumer passing `need`, and the span was never committed to
the ring.* Step 2 sharpens "committed" into the four positions
above, and the boot's log decides between three exact shapes:

- **Confirmed, as placed (unhanded span).** The dump shows the
  setter `C` event and a queued `S` (`OUT`), then **no `F` event
  at all** afterwards — not `b` (span < 16 KB and < 16 records),
  not `f`/`s`/`w`/`a` — while the guest spins and the executor
  sleeps. The records were written and accounted (`phead`
  covers them) but never *handed over*; the missing link is the
  absent hand-over trigger, and the fix conversation lands on
  the flush discipline (e.g. hand over when a store queues
  behind an unhanded gate), *not* on the delivery gate itself.
- **Refined (handed over, never made).** The dump shows an `F`
  reaching past the setter's end, `head` covering `need` — but
  step 1's line still shows `tail` parked short of it with the
  consumer idle. Then the span *was* committed and handed over,
  and the defect is in the consumer's walk (e.g. a header it
  cannot step over — a `W`-flagged wrap at exactly that span
  would name it). The (ii)-family placement dies; the fix moves
  to `q_thread`'s stepping.
- **Refuted (need itself is corrupt).** The setter `C` event's
  arithmetic does not reconstruct: its `start` is not the
  previous event's `end` (± wrap), or a `T` commit's `end`
  exceeds every later `phead` in the same dump — i.e. `p_head`
  went *backwards* after setting need (a rewind path the current
  code reading says does not exist). That would send the hunt
  into the reserve/commit accounting itself, with the dump
  naming the exact event pair where the positions diverge.

## Perturbation argument

The stall is a *control-flow* cycle, not a timing race: the
guest waits on a store, the store waits on a hand-over, and
every hand-over trigger is a discrete event (a flip, a sync, a
wait, a walk-end flush, a count/byte batch threshold) — none of
them is produced or suppressed by the executor running a few
nanoseconds slower. The instrument's hot-path cost is one
`q_ns()` plus ~10 plain stores into a static slot per record
commit, and the same again per flush/store/call/sync decision —
all on the executor thread only. It takes no lock, allocates
nothing, never touches the consumer thread, the ring bytes, any
position variable, or any gate; with the flag absent the hooks
are skipped before even the timestamp. The one timing-visible
change is the report line itself growing (printed once per
[perf] block, as step 1's did). Executor throughput perturbation
is bounded by ~2 timestamp calls per record — the same order
step 1 already added on the consumer side without masking the
stall (Window 1 reproduced it exactly, 9 draws and all).

## Build

`WORK_DIR=$HOME/jsrf-switch-build-zcinstr2 JSRF_GEN_OPT=-O3
SKIP_RECOMP=1 tools/switch/build.sh "<ISO>"` — exit 0, one held
session, under the build PC's shared build lock (taken
23:51:11Z after the Window 4 coordinator released it, released
23:58:52Z; the build and its SHA-256 are logged in the shared build notes). The linked NRO carries both the step-1 `zc-telem` and
the new `zc-acct` format strings, and its version string reads
`0.4.1-switch-8-ga8b6164`. Provenance was checked rather than
assumed: the patched source's sha256 was identical on the
authoring side and in the LOQ tree before commit (`63197d9c…`,
one file, +266/−14), and the NRO hash below was read twice,
~7 minutes apart, unchanged (the LOQ's storage-rollback guard).
Post-build tree re-verified: clean status, exactly
`94a3f10` / `3dc818e` / `a8b6164` on `53a83f2`.

- **NRO (LOQ):** `~/jsrf-switch-build-zcinstr2/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
- **Size:** 44,918,981 bytes (+8,192 over the step-1 NRO — the
  added report/event code in the same translation unit)
- **sha256:** `f070bc87bd95e27dc9fbebf33400bd3123186d54404fb98495689f9538ab7d9f`

Claim-protocol note, recorded plainly: an earlier read of
the build lock in this session was ~40 minutes stale; a first
lock write at 23:44Z overwrote a lock the Window 4 coordinator
had taken at 23:40:42Z. It was restored byte-for-byte within
~2 minutes, no build was started under it, and the collision is
disclosed in the shared build notes. The build above ran under a
lock taken after a fresh free-state read.

## The reproducing boot (spec for the future console run)

Identical to the Window 1 ZC boot (itself proof-window Leg 1's
A.1 arm), with the gate value as the only delta:

1. Console claim protocol + capture-verified foreground, as
   always. Stage this build's NRO to
   `switch/jsrf-unleashed/jsrf-unleashed.nro` (hash-verify on the
   card — hash in the Build section below).
2. options.txt: the Leg 1 arm's options state with both lines
   present: `RECOMP_QUEUE_COPY_VERTICES=0` (the mode under test)
   and `RECOMP_QUEUE_ZC_TELEM=2` (this instrument; level 2
   includes all of level 1's output). Nothing else changes.
3. Launch via the forwarder. Boot banner must show
   `vertex arrays read later (GPU progress held back;
   RECOMP_QUEUE_COPY_VERTICES=0)`.
4. Let it stall (expected: ~9 draws, black, guest alive). Hold
   **at least ~60 s** so several [perf] blocks print inside the
   stall; the zc-acct `need set by … ms ago` age must be seen to
   grow across at least two blocks while the event tail stays
   frozen.
5. Close via HOME → X → A, pull `Logs/last-run.log` before any
   relaunch, restore options.txt byte-exact afterwards.

**Reading the result:** find the first stall block; its
`zc-acct` line names the need-setter and its age; walk the
dumped events for (i) the setter's `C`, (ii) any `F` after it
and its reach, (iii) the queued `S` with `OUT`. Those three
reads select between the confirmed / refined / refuted shapes
above. A healthy contrast is available in the same log's early
blocks and in any `COPY_VERTICES=1` boot at gate 2 (commits
mostly flag-less — draws do not touch — `need` static, stores
`now`/`IN`).

## Not done, by design

No fix, no ordering/gating/delivery change, no soak, no console
run. The boot above is the ZC programme's next step; it runs
when the campaign commissions it.
