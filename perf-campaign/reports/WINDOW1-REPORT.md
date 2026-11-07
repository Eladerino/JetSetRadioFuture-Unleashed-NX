# Campaign console window 1 — S-0 counters runs + ZC reproducing boot (Nov 6, 2026)

Console claim held 2026-11-06T12:45:11-03:30 → released
~13:41 NST (deleted, absence read back). Foreground capture-verified
before first input on every boot (HOME + JSRF tile at claim; HOME
again before the ZC launch). All runs on the frozen pack
`dk_shaders.pak` 1,420,300 B / `2c3a820f…` (hash-verified before and
after the window; never rebuilt). Settings `92e6aa90…` verified
unchanged after every run. The save was never written.

**Card as-found note (differs from the brief, handled on the
as-found rule):** the resident options at claim time were **6,055 B /
`60968312…`**, not the prenight `1c2a2772…` — the candidate build had
appended its own two lines (`RECOMP_DK_POST_CRT=0`,
`RECOMP_DK_POST_VIGNETTE=0`, LF endings) during Brandon's play /
the install verification boot. That as-found file was snapshotted
(`~/flip/w1-options-asfound.txt` on the LOQ) and used as both the
run-options base and the restore target; it is back on the card
byte-exact (verified at close-out).

**Method note — the load chain (a real finding, cost the window
three attempts):** drive.py's blind load chain desynced at the
save-picker YES/NO dialog in attempts 1–3 (voids preserved; attempt
1 was additionally a GPU-307 boot, void on clocks regardless).
Isolation, step by step with captures: the dialog opens on NO and
**the D-pad does not move its selection at all on this setup**
(DLEFT ×2 and DRIGHT all inert, in the driver and by hand). The
selection follows the **left stick's level**: deflect left and YES
highlights, release and it falls back to NO — the confirm must be
pressed **while the stick is held**. (Run 1's load latched with a
single 0.35 s flick; Run 2's boot needed the held form — the
behaviour is level-following with per-boot latch variance.) Both
valid runs were loaded with the proof window's step-verified manual
chain (title → picker by gated script, dialog by held stick +
confirm, every step capture-verified) and driven with drive.py's
departure/patrol/close/pull machinery verbatim. Future campaign
windows should not use the blind chain for the dialog step.

---

## Job 1, Run 1 — counters dwell (S-0 build `0606cf3`, NRO `9ea0b4d3…`)

Valid on its 4th attempt (1: 307 boot + dialog desync; 2: dialog
desync, stock clocks, killed at the picker; 3: gated-driver abort —
dialog again). Options = as-found + `stats=1` + `sample=1`
(6,076 B / `0c06af61…`). Clocks **uniformly 1020/384/1331** in the
run's own [SWPERF] samples; purity Stg30 ×1, MAP/TRACK/CUSTOM 0;
segment = Stg30 entry (t=303.0) to log end, 8 patrol cycles,
311 [stats] windows in-segment. Log: `w1-s0run1.log`
(7,304,490 B / `66669de7…`). Frame context only (shares are the read): segment
400–699-draws hitch median 33 ms; pushbuffer median 257,625
words/frame.

### vp no-op rates (steady medians over 308 windows)

| class | words/frame | no-op |
|---|---|---|
| prog | 31,106 | **94.0%** |
| const | 17,034 | **71.0%** |
| xfctx | 2,735 | **98.7%** |

**Combined prog+const: 48,140 words/frame, weighted no-op 85.9%.**

Verbatim sample window: `[stats] vp uploads: prog 32547 words/frame,
30609 no-op (94.0%); const 18627 words/frame, 13053 no-op (70.1%);
xfctx 2891 words/frame, 2854 no-op (98.7%)`

### S-1 verdict — GO, priced at the top of its band (~0.5–0.6 ms)

The study's mapping: ~25% no-op ⇒ ~0.15 ms (defer); ~75% ⇒ 0.2–0.6 ms
(build next); ~0.15 ms per 25 points interpolated. The measured
**85.9%** sits above the 75% anchor ⇒ **S-1 is the largest single
state item and is built next, priced ≈0.5–0.6 ms.** The compound
term is as large as feared: `[stats] queue` medians per report
window — **rows_diff prog 10,352 calls, 1,407,940 rows scanned,
27,360 rows shipped** (a 51.5:1 scan-to-ship ratio; a no-op-only
upload burst currently still triggers the full 136-row scan).
rows_diff const reads 0/0/0 (const runs do not pass through
rows_diff). The xfctx no-op rate (98.7%) separately prices the
constant range-widening (rows 56–59) a gate would remove.

### Queue remainder split + S-3 / A.4 reads

Per-frame remainder medians: **1,227 KB/frame total — shadow 551,
fixed structs/headers/padding 436, const runs 233, prog runs 11.**

- **A.4 / S-5 branch verdict:** shadow bytes are the largest single
  component (45%) ⇒ per the study's test the branch does **not**
  close as fixed-cost noise; shadow traffic stays the live branch
  (B.3's context), fixed second.
- **S-3 refinement:** state records 891.5/frame, **dirty
  blocks/record mean 7.93**; histogram medians (records per window
  by bin): 0:0, 1:41, 2:422, 3:6,155, 4:2,494, 5–8:13,976,
  9–16:14,576, 17–32:430, 33–64:0, 65+:0. The mass sits at 3–16
  blocks against regs_collect's 128-flag scan — the scan is ~94%
  empty flags, so S-3's mechanism still applies, but records are
  not tiny: **S-3 stays live inside its quoted 0.1–0.25 ms band,
  neither firmed to the top nor dropped.**

### Dwell shares re-run ([SWPROF], same run)

Walk thread (tid 1001), whole-run prof-pcs: 425,954 samples, 0 lost
(`w1-prof-pcs.txt`, `9bdc0d1a…`; symbolised against the campaign
build's ELF — note: prof.sh first symbolised the Nov 5 dwell's
prof-pcs by mistake, caught on the sample counts and re-run against
this run's file). Waits 67.2% of thread samples. Active-compute
split, classified exactly as WALK-ATTRIBUTION-2026-11-05:

| class | this run | Nov 5 dwell |
|---|---|---|
| STATE | **42.5%** | 39.0% |
| DECODE | **35.2%** | 37.0% |
| DATA | **17.0%** | 17.5% |
| LOOKUP | **3.1%** | 4.2% |
| OTHER | 2.2% | 2.3% |

The attribution is stable: state+decode co-dominant (~78%
combined), data ~17%, lookup a non-target. (Population differs
slightly — whole session incl. title/garage vs the Nov 5 run's mix,
and the stats counters are on; direction and ranking identical.)

## Job 1, Run 2 — REST census (same S-0 NRO; options as-found + `RECOMP_PB_REST=1`)

Valid first attempt. Clocks uniformly 1020/384/1331; purity clean;
one patrol (4 cycles); 134 census windows in-segment. Log:
`w1-s0run2.log` (`fcf1a703…`).

Steady segment census (all 134 windows in two shapes, verbatim):
`the rest by method: 1EA4 1% 17FC 1% 1B4C 1% 1720 0% 1760 0% 1784 0%`
(83 windows) / `… 1B4C 0% …` (39) — the rest is 18% of ~278K
words/frame and **no rest method exceeds 1%**, confirming the
in-tree prior for the tower patrol.

### S-4 verdict — NO-GO (gate not met)

SET_COMPOSITE_MATRIX is method **0x0680** (nv2a_regs.h:1065),
SET_VIEWPORT_OFFSET **0x0A20** (:1081). In the patrol segment:
the composite lead bucket `0680` reaches the top-6 in **exactly 1
of 134 windows, at 0%**; `0A20` **never appears**. (Pre-segment,
in the garage/departure phase, 0680 sits in the top-6 at 0% in
70/89 windows and 0A20 surfaces 8 times — S-4 was priced on the
tower patrol specifically.) Composite + viewport as printed never
approach the ≥ ~0.75% scheduling gate ⇒ **S-4 is not built.**
Caveat, recorded plainly: the census counts per method bucket and
a 16-word composite run spreads over buckets 0680–069C, so the run
total is up to 16× the lead bucket's share and the integer-%
print rounds anything under 0.5% per bucket to 0 — the census as
designed cannot put a hard sub-0.75% proof on the run total. On
the pre-registered read (the printed census) the gate fails
clearly, and the study's own ceiling for S-4 at even a leading
share was 0.1–0.2 ms.

---

## Job 2 — ZC reproducing boot (telemetry build `3dc818e`, NRO `b5790517…`)

Options = Leg 1 arm state (baseline manifest + both lines,
6,038 B / `a0406bfb…`). Boot banner confirms the mode engaged,
verbatim: `[QUEUE] back end on a thread of its own: 8192 KB ring,
up to 2 frames queued, 50 us spin, vertex arrays read later (GPU
progress held back; RECOMP_QUEUE_COPY_VERTICES=0)`, with
`RECOMP_QUEUE_ZC_TELEM=1` in the options echo. Stall reproduced
exactly as Leg 1: **9 draws** in the first [perf] block, then 0
draws / 0 words forever, pure black screen, guest alive. Held
~2 minutes (64 [perf] blocks print inside the stall), HOME-closed,
log pulled before any relaunch: `w1-zc.log` (`d6fd550c…`). Clocks
uniformly stock (immaterial here, recorded anyway).

**The stall is present from the first [perf] block** — every one
of the 64 zc-telem lines is identical in shape; verbatim (block 2
and every later block):

```
[perf] zc-telem: consumer idle (between records); ring tail 51104, head 51104, need 56016 (5 KB unmade ahead of need); made/frame: draw 0.0 (0.00 ms), clear 0.0 (0.00 ms), flip 0.0 (0.00 ms), store 0.0, call 0.0; stores queued behind draw 0.0/frame, flip 0.0, other 0.0; with a flip ahead 0.0
```

Companion queue lines in the stall (verbatim):
`[perf] renderer thread: idle 0%; queue 0 KB/frame …, 0 records/frame in 0 batches, 0 made on the executor; the executor waited 0.00 ms/frame (traps 0.0/frame 0.00 ms, until idle 0.0/frame 0.00 ms, two frames ahead 0.00 ms, ring full 0.00 ms); stores 0.0/frame queued, 0.0 at once`
`[perf] pushbuffer: 0 words/frame …; executor per frame: 1.0 ms in the walk (0.0 ms of it stalled on 0.0 NOP traps), 1993.3 ms asleep (9790.0 sleeps; new commands seen 0.0 times while watching before a sleep); woken 0 times by a guest wait`

Block 1 (the only transitional block): made/frame draw 0.1 /
clear 0.3 / flip 0.1, `stores 0.2/frame queued, 0.1 at once`, and
the executor's only nonzero wait of the boot: traps 0.1/frame,
1.43 ms. The guest stays alive throughout — [KERNEL] summaries
keep advancing (ordinal 231 ×22,925; 143 ×15,294; 159 ×15,280 by
the last block).

### Cycle verdict (against DECODE-DATA-STUDY §2.5's three candidates)

- **(i) flip-overtaking — REFUTED (proven by the prints).** The
  consumer is never inside a flip: it reads `idle (between
  records)` in all 64 blocks, flip made/frame is 0.0 in the stall,
  no store is ever queued behind a flip, `with a flip ahead` is
  0.0 throughout. Leg 1's prime suspect is not what this boot
  died of.
- **(iii) big-record path — REFUTED (proven by the prints).**
  `made on the executor` is 0 in every block and the until-idle
  (W_ALL) wait is 0.00 ms throughout; tail ≠ need besides.
- **(ii) trap-sync / software interlock — CONSISTENT-WITH in
  family, with the mechanism refined by the readings.** The
  consumer is exonerated exactly as (ii) predicts (idle, tail at
  head), but the stall is not carried by trap counters and the
  consumer is starved rather than cycling: the executor is
  *asleep* (1,993 ms per block, woken 0 times), all its
  instrumented waits read 0. What the telemetry actually shows:
  **`need` sits at 56016 — 4,912 B past `head` (51104) — from the
  first block onward, and nothing ever satisfies it.** A progress
  store was queued early (block 1: 0.2/frame queued) behind that
  `need`; in read-later mode its delivery is gated on the
  consumer passing `need`, and the 4,912 B span between head and
  need was never committed to the ring — so the gate can never
  open, the guest spins on kernel calls waiting for the store,
  produces no new pushbuffer, and the executor sleeps. The cycle
  lives between the guest and the queue's progress accounting,
  not inside any consumer call. Why the executor advanced
  `p_need` across a span it never committed is the one link this
  instrument cannot see (executor-side write/need accounting was
  not instrumented) — that is the ZC-FIX programme's next
  instrument, and the fix conversation belongs on the
  store-delivery gate (`now_ok`/`queue_sync`), as §2.5's
  requirement 3 anticipated.

Labels: the telemetry readings are proven (they are the prints);
the (i)/(iii) refutations follow directly from them; the
(ii)-family placement and the store-gate mechanism are
**consistent-with** the readings, with the uncommitted-span link
explicitly unproven.

---

## Close-out verification (all read back over the console file service after restore)

| item | value | |
|---|---|---|
| resident NRO | 44,910,789 B / `084455ca46828a4416b26ccdb628720df899f29bc11299d92387b561df6234e6` (fixed candidate) | ✓ |
| options.txt | 6,055 B / `609683123e208107a041cae638d2829485a8079fbf35e2ebe3d8d66bfa2f4ea5` (as-found) | ✓ |
| settings | 246 B / `92e6aa904d60727b4d309c4eff5474cf9f8abe986e4cf81e479d5e8318d43105` | ✓ |
| dk_shaders.pak | 1,420,300 B / `2c3a820f649c57bb0bfb04cf5437af73158f1be05ae467bed81086c380a939c9` (unchanged) | ✓ |
| console | game closed, HOME menu, JSRF tile focused (capture) | ✓ |
| console claim | released, absence read back | ✓ |
| virtual-pad tool config | buttonClickSleepTime restored to 50 (controllerIdleReleaseMs was already 0 on arrival; left 0) | ✓ |

Close-out stumble, recorded: the first restore attempt pulled the
NRO from `~/jsrf-proof-window-backup/` — that backup is the
*shipped* build (`211f4778…`), predating the candidate. Caught by
the hash check before any launch; the candidate was restored from
its preserved copy (`Downloads/jsrf-build-menufix.nro`,
`084455ca…`) and verified by full pull-back.

## Evidence (this folder)

- `w1-s0run1.log` (`66669de7…`) — counters dwell, valid.
- `w1-s0run2.log` (`fcf1a703…`) — REST census, valid.
- `w1-zc.log` (`d6fd550c…`) — ZC reproducing boot.
- `w1-s0run1-void1.log` (`852979da…`) — attempt 1 (307 boot +
  dialog desync). `w1-s0run1-void2.log` (`34fff773…`) — attempt 2
  (stock clocks, dialog desync, killed at the picker).
- `w1-prof-pcs.txt` (`9bdc0d1a…`) — Run 1 whole-run samples;
  `w1-prof-whole-run.txt` — symbolised whole-run tables.
- Run captures and drive logs remain on the LOQ under `~/flip/`
  (`shots/w1-s0run1`, `shots/w1-manual*`, `w1caps/`, `logs/`).

## Decisions handed back to the campaign

1. **S-1: GO — build next, priced ≈0.5–0.6 ms** (85.9% combined
   no-op; rows_diff scan:ship 51.5:1).
2. **S-3: stays live** in its 0.1–0.25 ms band (mean 7.93
   blocks/record; scan ~94% empty).
3. **A.4: shadow branch stays live** (shadow 551 of 1,227 KB/frame
   remainder is the largest share); fixed-cost closure rejected.
4. **S-4: NO-GO** — census gate not met on the tower patrol.
5. **ZC-FIX:** candidates (i) and (iii) refuted; the wait is a
   progress store gated on an unsatisfied `need` 4,912 B past the
   committed head. Next instrument: executor-side write/need
   accounting at the stall's first frames.
