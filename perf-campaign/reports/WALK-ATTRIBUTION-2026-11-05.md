# Walk attribution dwell — executor walk split (2026-11-05)

Campaign's last commissioned run, after the LIVE COMPARE matrix,
S2 and the replay verification pass. Purpose: split the executor
walk's ~19.3 µs/draw into decode vs state-application vs
lookup/caching, with named hot functions — the target list for
§8 lever 1 of the perf campaign.

## Method

- Build: landed v0.4.1-switch (NRO 211f4778…), **no build was
  needed**: the sampler is an options toggle on the shipped code
  (`sample` → `JSRF_SAMPLE`, mapping at
  `port/switch/jsrf_switch.c:541`; gate at `start_perf_log`,
  jsrf_switch.c:1166, `atoi > 0`). Options = campaign baseline +
  `sample=1` (5,993 B, 4736aef2…), byte-exact read-back.
- Run `flip-20261105-dwell-prof`: standard 99th Street patrol
  route at 8 cycles (two full patrols), first attempt valid —
  clocks uniformly 1020/384/1331, purity clean, segment span
  ~582 s. Latch in-log: `[SWPROF] sampling every 2 ms`.
- Instrument: JSRF_SAMPLE sampling profiler (500 Hz, pc+lr+
  4-deep chains; [SWPROF] 10 s windows in the run log,
  whole-run `Logs/prof-pcs.txt` pulled beside it: 255,424
  samples on the walk thread, 0 lost).
- Symbolisation: `tools/switch/prof.sh` against the landed
  build's own ELF (`~/jsrf-switch-build-dk/dist/debug/
  jsrf_recomp.elf` — identity proven: that tree's dist NRO
  hashes to the resident 211f4778…). prof.sh's default ELF is
  a stale Nov 3 build and was NOT used.
- **Sampler perturbation, measured: +1.0 ms.** The dwell's
  combined staircase median is 34.0 ms vs the 33.0 baseline,
  buckets uniformly one quantum up (34/35/39 vs 33/34/39).
  Attribution shares below are proportions of sampled time
  and are not corrected for this; the cost is on record as
  commissioned.
- **Blind spot (known, not fixed here):** the sampler covers
  the engine + Win32-compat threads. The deko3d renderer
  thread's internal work (the separate ~10 µs/draw "renderer"
  component in [perf]) does not register and is outside this
  attribution. This document attributes the **walk thread
  only**.

## Which thread is the walk

`tid 1001` — the dedicated pushbuffer executor (its chains root
at `nv2a_pb_walk_range < nv2a_ack_thread`). The guest engine
thread is a different thread (86.7% generated game code:
recomp_p32 15.5% self, etc.) and is not the walk.

Of tid 1001's whole-run samples, **68.4% are waits**
(svcWaitProcessWideKeyAtomic 67.4 + svcSleepThread 0.5 +
sync SVCs ~0.5). In the Stg30 windows the wait share is
63.7%, and the inclusive chain table shows essentially all
of it is one chain: `svcWait… < nv2a_ack_wait` at 63.4% —
the executor starved for guest pushbuffer, i.e. paced by
production, not blocked on its own work. (Individual windows
also show a `prod_wait < q_flip` back-pressure chain, up to
~12% in the arrival window; it is not stably quantifiable
from the printed top-12 chains and is not in the split.)

The split below is over the remaining **active compute:
30.8% of thread samples** (99.2% of them individually named
in prof-pcs). Cross-check: sampled active ≈ 10 ms/frame of
the 33 ms frame at the patrol; the [perf] "in the walk"
figure (~16 ms) is the walk's own wall clock and includes
in-walk queue waits, so ceilings quoted against ~16 ms are
upper bounds in the campaign's framing.

## The split (share of active walk compute)

| Class | Share | Ceiling vs ~16 ms walk |
|---|---|---|
| State application | **39.0%** | ~6.2 ms |
| Decode | **37.0%** | ~5.9 ms |
| Data movement (memcpy/memset) | **17.5%** | ~2.8 ms |
| Lookup / caching | **4.2%** | ~0.7 ms |
| Other (thread mgmt, instrumentation) | 2.3% | ~0.4 ms |

**The headline: there is no single throat.** Decode and
state-application are co-dominant (~37/39), data movement is
a real third (~17.5), and **lookup/caching — one of the
three candidate explanations going in — is a rounding
error at 4.2%**. A lever-1 fix has to shave the top of the
decode and state piles together; no cache trick pays.

## Hot functions (self time, share of active compute)

| Function | Share | Class | Notes |
|---|---|---|---|
| memcpy | 16.9% | data | leaf; called as `memcpy < q_draw < raw_batch < nv2a_pb_exec_method` — vertex/index data copies on the draw path |
| nv2a_pb_exec_run | 15.9% | decode | the walk loop core (nv2a_pb_exec_dk.c:2530 hot line) |
| nv2a_pb_scan | 11.4% | decode | pushbuffer scan (nv2a_pb_scan.c:690) |
| nv2a_pb_exec_method | 8.1% | state | method dispatch + handlers |
| note_unhandled | 5.2% | decode | unhandled-method bookkeeping (with unhandled_on 0.6 + rest_note 0.6: ~6.4% for the unhandled path in total) |
| q_draw | 5.2% | state | draw queueing to the backend |
| shadow_write | 4.2% | state | shadow state writes |
| vp_upload_run | 2.6% | state | vertex-program constant uploads |
| shadow_run | 2.6% | state | |
| regs_collect | 1.9% | state | register collection for state capture |
| win_match | 1.9% | lookup | the largest lookup there is — still under 2% |
| pb_word | 1.6% | decode | |
| vp_method | 1.6% | state | |
| raw_batch | 1.6% | state | parent of the memcpy chain |
| state_write / rows_diff / capture_transform / capture_render_state | 1.3% each | state | the state-capture quartet ≈ 5.2% together |
| pb_win | 1.3% | decode | |
| record_tex_reg | 1.0% | lookup | |
| raw_direct_attrs / describe_state | 1.0% each | state | |

Inclusive read: essentially 100% of walk compute hangs
under `nv2a_pb_scan < nv2a_pb_walk_range` — one root, no
second entry point worth a separate lever.

## Ranked lever-1 target list (ceiling = share of ~16 ms walk)

1. **Data copies on the draw path (memcpy under q_draw/
   raw_batch)** — ceiling ~2.7 ms. The single largest
   function in the walk is a copy. Any zero-copy or
   copy-avoidance win here is the biggest single-function
   prize available.
2. **The walk loop + scan pair (nv2a_pb_exec_run +
   nv2a_pb_scan)** — ceiling ~4.3 ms combined. The decode
   core itself: loop structure, word fetch, range walking.
3. **Method dispatch and state handlers
   (nv2a_pb_exec_method + shadow_write/shadow_run +
   vp_upload_run)** — ceiling ~2.9 ms combined. Includes
   the state-capture quartet (~0.8 ms) as a sub-target:
   regs_collect/capture_transform/capture_render_state/
   rows_diff exist to snapshot state — worth auditing what
   actually needs capturing per draw.
4. **The unhandled-method path (note_unhandled +
   unhandled_on + rest_note)** — ceiling ~1.0 ms. Pure
   bookkeeping for methods the build does not implement;
   the cheapest kind of time to delete (rate-limit or
   compile out the noting) — small, but nearly free to take.
5. **Lookup/caching as a class** — ceiling ~0.7 ms total.
   Recorded as a NON-target: win_match, record_tex_reg,
   dma_resolve and friends are collectively negligible.
   Lever 1 should not be spent here.

Realistic expectation, stated plainly: these are ceilings
(total elimination), not estimates. The walk is broad —
the top function is 17% and the top three classes must all
move to buy a frame quantum at the plaza (33.0 → sub-33
needs ~1 ms+ at the median bucket; the 400-bucket sits at
33.0 with the frame's other costs inside the same quantum).

## Artefacts

- Run log + drive log: LOQ `~/flip/logs/flip-20261105-dwell-prof.*`
- Whole-run samples: LOQ `~/flip/logs/prof-pcs.txt`
- Symbolised profile: LOQ `~/flip/logs/dwell-prof-symbolised.txt`
- Classification script: LOQ `~/flip/classify.py`;
  Stg30 window aggregator: LOQ `~/flip/agg-swprof.py`
