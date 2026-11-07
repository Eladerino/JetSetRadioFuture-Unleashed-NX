# Campaign B.2 — NEON AE16 unpack: build report (desk only)

**Verdict: desk-complete and harness-proven.** The scalar
ARRAY_ELEMENT16 unpack in `nv2a_pb_exec_run` now has a NEON form
behind `RECOMP_PB_NEONIDX`, gated by a bit-exact harness run of
**64,984 cases, 0 failures** — executed as real NEON on emulated
aarch64, not lane emulation. The counter split (§1.4 of
DECODE-DATA-STUDY.md) ships in the same NRO and prices AE32 from
the A/B's own runs. AE32 was **not** vectorised. No console
contact, nothing pushed.

Spec: DECODE-DATA-STUDY.md §1.4 (mechanism, price 0.5–1.0 ms
realistic, medium risk, harness-gated), §0.2 (index words are
61–67% of heavy-window pushbuffer words; the AE16/AE32 split was
the open measurement this build's counters close).

## Tree and commits

LOQ fork `~/jsrf-nx-neon` (copy of `~/jsrf-nx-rc042`), branch
rc042, three local commits on 53a83f2, no push:

| Commit | What |
|---|---|
| `0ceb3fe` | Campaign base: menufix.patch applied (verified `git diff --stat` = one file, +23, `src/third_party/uam/source/uam_api.cpp`) and committed as Eladerino |
| `4967b07` | Counters: index words split into AE16/AE32 in the [perf] pushbuffer line |
| `d7ddbde` | NEON AE16 unpack behind RECOMP_PB_NEONIDX + shared kernel header |

Combined patch (counters + NEON, against the menufix base):
`neon-b2.patch` in this folder, 10,079 B, `git apply --check`
clean against the menufix tree. Harness sources preserved in
`neon-harness/` in this folder.

## 1. The harness (the gate) — design and results

The kernel lives in one header, `src/kernel/nv2a_ae16_neon.h`,
included by **both** the executor and the harness — the code the
harness proves is the code that ships, not a copy that can drift.

- **Scalar side:** the shipped case's behaviour copied verbatim —
  capacity clamp `k = min(n, (NV_MAX_INDICES − idx_count)/2)`,
  lo/hi seeded from the batch's running range, per-word
  mask/shift + min/max + two u32 stores, and *nothing at all*
  written when `prim == 0`.
- **NEON side:** the same wrapper with the kernel where the
  executor's toggle puts it. Kernel: per 4 guest words, one u32
  vector load; the data read as u16 lanes is already the index
  sequence in order (guest word j holds index 2j low, 2j+1 high),
  so `ushll`/`ushll2` widening produces the two u32 store vectors
  with no shuffle; range accumulation is vector `umin`/`umax`
  seeded with the batch's running lo/hi and reduced horizontally
  once per vector stretch; the `k % 4` tail runs the scalar ops
  verbatim.
- **Compared per case:** the full touched span of `idx[]`
  (sentinel-filled buffers, guard margin past the run, so a stray
  write diffs), plus `idx_count`, `idx_lo`, `idx_hi` — all must be
  bit-identical.
- **Coverage:** structured edges — 14 starting counts (0, 1, 2, 3,
  100, 1000, and NV_MAX_INDICES−1/−2/−3/−8/−9/−16/−17/−full, i.e.
  clamp truncation at the capacity edge in every residue), n =
  0…20 (n=0 and every tail length), prim 0 and 1, six word
  patterns (random, all-zero, all-ones, ascending pairs,
  descending pairs, tiny-range), three seed shapes (fresh batch
  lo=0xFFFFFFFF/hi=0, mid-batch 1234/54321, frozen 0/65535 where
  the range cannot move) = **10,584 cases**. Randomised —
  50,000 small (n≤40), 4,000 medium (n≤600), 400 large
  (n 2000–4096 with counts within 9,000 of the clamp edge) =
  **54,400 cases**.
- **Execution:** compiled with zig cc 0.17.1
  (`-target aarch64-linux-musl -static -O2`, clang, real
  `<arm_neon.h>` intrinsics) and run under qemu-aarch64 10.2.1
  (user-space installs on the LOQ: pip ziglang + the Ubuntu
  `qemu-user` package extracted to `~/neon-tools` — no sudo, no
  system change). The harness binary's own disassembly contains
  the vector `umin`/`umax` sequence, so the gate exercised the
  vector path, not a scalar fallback.

**Result: 64,984 cases, 0 failures — BIT-EXACT.** Had any case
diverged, the item would have died here per the spec; none did.

## 2. Counters (commit 4967b07)

`s_ws.idx` split into `idx16`/`idx32` at the two cases in
`nv2a_pb_exec_dk.c`. `nv2a_pb_exec_words` now returns 8 values:
out[3] stays the index total (the [perf] caller's "the rest"
arithmetic is unchanged), out[6]/out[7] carry the split. The
[perf] pushbuffer line now prints:

```
[perf] pushbuffer: N words/frame (transform constants A%, programs B%,
 indices C% (AE16 D%, AE32 E%), inline vertices F%, shadow-only state
 G%, the rest H%); …
```

Format string verified present in the built NRO. This ships in
both A/B arms, so the split is read from the proof runs
themselves (§5).

## 3. NEON path (commit d7ddbde)

- `src/kernel/nv2a_ae16_neon.h` (new, +77): the kernel above.
- `nv2a_pb_exec_dk.c` (+25): `#include` of the header; a read-once
  toggle reader in the file's existing style
  (`RECOMP_PB_NEONIDX`, on iff the value starts with '1'); the
  AE16 case calls the kernel when the toggle is on.
- **OFF = shipped, byte-for-byte:** absent or any other value
  takes the scalar loop, whose text is untouched (the diff shows
  it only gaining an `#if defined(__aarch64__)` branch above it).
  The write-everything behaviour under `s_gpu.prim` is preserved
  — the branch sits inside the same `if (s_gpu.prim)` block and
  the kernel writes every index the scalar loop writes;
  `shadow_run` and the counters run identically in both modes.
- AE32 untouched, per spec — the counters price it first.

## 4. Build and ELF check

Build (held foreground session, exact prescribed command;
WORK_DIR `~/jsrf-switch-build-neon`, `JSRF_GEN_OPT=-O3`,
`SKIP_RECOMP=1`): **exit 0.**

- NRO: `~/jsrf-switch-build-neon/dist/switch/jsrf-unleashed/jsrf-unleashed.nro`
  (LOQ), **44,910,789 B**, sha256
  **`0968e372b8439305a2c6b9017e7013a587aa71a39ed78fbb6f383e4723215230`**
- Version string: `0.4.1-switch-8-gd7ddbde`.
- Build.sh's Windows staging copy (`Downloads/jsrf-unleashed-switch/`)
  hash-matches the dist NRO exactly.
- Post-build tree re-verification (LOQ storage-rollback guard):
  `git status` clean, all three commits intact — no rollback.
- `RECOMP_PB_NEONIDX` string present in the NRO (the toggle is
  reachable); AE16/AE32 format string present (counters live).

**ELF check (the first study's cautionary tale, re-run):**
disassembly of the built `nv2a_pb_exec_dk.o`'s
`nv2a_pb_exec_run` contains the kernel inlined as a 28-instruction
vector block: `dup` (accumulators seeded from lo/hi), `uxtl` +
`uxtl2` (objdump's names for the ushll/ushll2 widening),
`umin`/`umax` `.4s` ×2 each (accumulation over both widened
halves), `umin`/`umax` `.2s` + `uminp`/`umaxp` (the horizontal
reduction). Contrast: the same function in the scalar
(menufix-base) build's object contains **zero** such
instructions — the first study's zero-NEON finding, and the
difference is exactly this change.

**Operational notes (honest):** the build took three attempts.
Attempt 1 OOM-died in ISO unpack (`Cannot allocate memory`) with
two sibling campaign builds running — the LOQ's WSL has 15 GB
and three `-j4` builds do not fit; the siblings were left
undisturbed. Attempt 2, launched detached, was reaped when its
spawning session ended (WSL session lifetime — the reason the
campaign's held-foreground rule exists). Attempt 3 ran in a held
session once the build slot was free and completed in ~6.5 min.
While waiting, WSL stalled once for several minutes under the
parallel load and recovered; no sibling build was touched.

## 5. Patrol A/B spec (what makes this a result)

One NRO (this build), two arms differing only in options.txt:

- **OFF arm:** `RECOMP_PB_NEONIDX` absent (shipped scalar).
- **ON arm:** `RECOMP_PB_NEONIDX=1`.

Protocol per arm, per the campaign's uniform spec:

1. 99th Street entry-plaza patrol on the blueprint recording
   (FWD 14 s / BACK 28 s / FWD 14 s × 4, Game Data 1 load),
   replay-driven.
2. **Stock-clock gate:** the run counts only if its own [SWPERF]
   samples are uniformly 1020/384/1331; shader pack frozen
   (the `2c3a820f…` pack), dynamic res as in the baseline runs.
3. **Primary instrument: [perf] walk µs/draw** (sub-quantum walk
   savings are real even when the frame bucket does not move —
   the S2 precedent). Frame-time verdict against the pooled
   **33.0 ms** baseline: combined 400–699 median + sub-buckets.
4. **Replay must be bit-exact** (frame-exact against the
   blueprint landmarks): the harness proved the products equal,
   so any replay divergence on hardware kills the item rather
   than being explained away.
5. **The split read:** report the [perf] pushbuffer line's
   AE16 % and AE32 % from *both* arms' runs (they must agree
   between arms — same words, differently unpacked). The AE32
   share is the input to whether AE32 vectorisation is ever
   worth a card; if AE16 is the overwhelming share, B.2's
   coverage of the index population is confirmed from data.
6. Optional dwell arm (sampler on): `exec_run`'s share of active
   compute should fall in the ON arm; `nv2a_pb_exec_run` was
   15.9% self in the attribution dwell.

Expectation, honestly held to the study's pricing: 0.5–1.0 ms of
walk time at the plaza; the verdict is carried by walk µs/draw
first and the 400-bucket second.

## Not done / not claimed

- No hardware run of any kind (desk-only commission).
- AE32 not vectorised (deliberate; counter-gated).
- Nothing pushed; the rc042 branch commits are local to the LOQ
  fork. The menufix base commit is campaign infrastructure, not
  part of B.2's patch.
