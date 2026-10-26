# JSRF NX: handoff back to Claude (Sunday Oct 25, 2026, evening session with Sudo)

## Where things stand

The game boots and plays on real hardware (logos, title, hideout, Dogenzaka) and the phase is optimisation and fixes. The repo is private (github.com/Eladerino/JetSetRadioFuture-Unleashed-NX, main) and stays private until the sp00nznet Discord check-in. HEAD is now 4e60e714, which is also the build currently on Brandon's card.

Tonight's session started from your handoff (fast_gpu freeze at the save load) and ended with three fixes pushed and one new, well-characterised crash in the vertex cache. The one-paragraph version: the loading freeze is fixed and confirmed; the character select area after the pick is now the fragile spot, with one crash fixed tonight (pushbuffer walk hop) and one identified but not yet fixed (vertex cache hash, test queued that needs no rebuild).

Full run-by-run analysis with log excerpts: TEMP-HANDOFF-ANALYSIS-2026-10-25.md in Sudo's workspace (Brandon can pass it over). The logs themselves (last-run-2.log through last-run-9.log, prof.txt through prof-6.txt) are files Brandon uploaded to Sudo; ask him if you need one.

## Hard rules (unchanged)

- Game files, the disc image, generated C (port/recomp/gen), and the .nro/.elf never go in the repo and never leave Brandon's PC. Only logs cross over.
- Brandon builds on his LOQ under WSL2 (checkout ~/jsrf-nx): `git pull`, then `SKIP_RECOMP=1 tools/switch/build.sh "/mnt/c/Users/glynn/Downloads/JSRF - Jet Set Radio Future (USA).iso"`.
- Run tools/switch/prof.sh BEFORE rebuilding; it needs the .elf that matches the run. addr2line for crash addresses: /opt/devkitpro/devkitA64/bin/aarch64-none-elf-addr2line -e ~/jsrf-switch-build/dist/debug/jsrf_recomp.elf -f -C <nro+ offsets from the [CRASH] block>.
- build.sh writes options.txt only if missing, so option changes are hand edits by Brandon. He wants test settings in options.txt, not extra WSL commands.
- Upstream src/ stays free of external dependencies; Switch behaviour gated by __SWITCH__ or an env option.
- Commit messages in repo style, state what was actually tested, no AI attribution lines.
- Test setup: Atmosphère emuMMC, title takeover, overclock CPU 1963 / GPU 1152 / mem 2133 MHz. Baseline FPS at fast_gpu=0: title 26.5, hideout 51, Dogenzaka cutscene 29. Target 60.

## Fix 1 (confirmed): make-space across a pushbuffer wrap

Two halves, both in the flush-ack machinery.

Branch choice (port/recomp_manual.c, the sub_001910E0 wrapper around the recompiled spin at gen sub_00191440, x86 0x001914F0). The spin is a plain unsigned compare with no wrap handling: it waits while the word reached via the device struct +0x34 is below the target word, and positions are numbered linearly. Across a ring wrap (DMA_GET numerically ahead of DMA_PUT) that test can never succeed, because the ack thread republishes GET as the new smaller PUT and the waiting title submits nothing more. The old override forced the spin for large distances; D3D's own answer takes the spin for small ones too. Rule now: across a wrap, always take the sleeping branch (leave a large answer alone, raise a small one into sleep range); linear cases clamp to 0x7FFF as before. The sleeping branch wakes on software method 5.

The walk's jump search (src/kernel/xbox_memory_layout.c, ack thread). On a wrap the walk searched for the ring's JMP in a fixed 128 KB window, but the jump sits at the ring's end. Run 5's last PUT was 0x4D3A0 in a ring running past 0x80000, a tail of about 208 KB, so the jump was never found, the head segment holding make-space's NOP(5) was never walked, and the wake-up was never even counted. Bound widened to 0x100000 (1 MB); the walk stops at the jump, so the bound is a cap, not a cost.

Commits: f10de665 (JSRF_SPIN_LOG instrumentation), 4f9d78b9 (v2 wrap guard, large distances), 8525ccd5 (jump search bound + label fix), 50310ef1 (v3, wrap rule covers any distance), 281fcc51 (made permanent: wrap rule applies whenever fast_gpu is on, the [SPIN] poll went back to throttled 1/sec behind JSRF_SPIN_LOG, per-call logging removed).

Evidence: run 6 (v2 + walk fix) got method 5 counted and delivered 200/200 and presents reached frame 16,189, against run 3's freeze at frame 7,398 with method 5 never counted. Run 7 (v3) ran about 273 s with zero FROZEN reports, method 5 delivered 1,141/1,141 across 1,717 make-space calls. Run 8 (permanent build, normal options) had 3 wrap events, all slept, 45/45 delivered.

Testing change that follows from 281fcc51: normal fast_gpu tests now need only fast_gpu=1 in options.txt. The JSRF_SPIN_LOG=1 line comes OUT; it now only enables the throttled poll and costs performance. This is also written up in docs/switch-testing-notes.md in the repo.

Honest caveat: of run 7's 248 wrap calls, all were large-distance. The small-distance wrap raise in v3 has never actually been exercised, so treat that half as insurance in place, not as proven.

## The character select cluster: runs 6, 8, 9

Three incidents, all after the character pick, during the character's dance (the sequence loads Stg00_04 / Stg00 data):

- Run 6 froze there (audio skipping, animation stuck, inputs dead, about 74 s observed). Back in the spin tail with no clamp involved. The fatal make-space call fell between the throttled [SPIN] polls, so its numbers were never logged. STILL UNEXPLAINED.
- Run 8 crashed a few seconds into the dance. FIXED, see Fix 2.
- Run 9 (hop-guard build) flickered visually from early on, reached the end of the dance, and crashed in the vertex cache. IDENTIFIED, NOT YET FIXED, see below.

Do not assume these three are one bug. They share a location, not yet a cause.

## Fix 2 (pushed, held in run 9): walk subroutine jump hops

Run 8's crash: data abort, FAR = guest 0x8F800000. addr2line put it in pb_word (src/kernel/nv2a_pb_scan.c:245, the fallback read for a VA outside the snapshot windows), called from pb_walk_sub's hop loop (:363/:365) under nv2a_ack_thread.

pb_walk_sub has validated CALL targets against the 64 MB RAM size since an earlier fault (its comment tells that story: a CALL to 0x84AF04AC). The jump hops it follows out of a subroutine were canonicalised with the 28-bit mask and followed unchecked, so a hop to 0x8F800000 was scanned and its first word read at an unmapped address. Commit 4e60e714 gives the hops the same RAM-size test; the chain simply ends. Run 9 ran the same pick-and-load path with no fault of this class, so the fix holds for the case that produced it. Also in 4e60e714: the run 8 outcome appended to docs/switch-testing-notes.md.

## Current bug: vertex cache hash reads off the end of a draw (run 9)

addr2line for run 9 (build 4e60e714), from the [CRASH] block:

- PC = vc_hash, src/nv2a_gl/nv2a_gl.c:3960 (the 32-byte hash loop, reading the data pointer)
- vc_content, nv2a_gl.c:4202 (the vc_hash call; the crash register x2 held vc_content's seed constant 0x3C6EF372FE94F82B, confirming the site)
- vc_find, nv2a_gl.c:4060
- do_draw_body, nv2a_gl.c:4696 (the vc_prepare call)
- nv2a_gl_method, nv2a_gl.c:9971
- nv2a_pb_exec_method, src/kernel/nv2a_pb_exec.c:1512
- nv2a_pb_scan, src/kernel/nv2a_pb_scan.c:334 (the method-data exec site)
- FAR = 0x755f72cf00, a HOST address, not a guest VA.

Mechanism, from reading the source: vc_prepare (about nv2a_gl.c:4240-4273) computes lo/hi as the min/max over the draw's index list, then vc_content hashes, for each live attribute array, the span base + lo*stride through base + hi*stride + size. Nothing clamps hi against the array's actual extent in guest RAM. One wild index value and the hash walks host memory until an unmapped page. The dance is index-heavy skinned drawing, exactly the path vc_prepare serves (non-inline draws with out_n >= VC_MIN_N).

Leading theory, labelled as theory: milder bad index data explains the visual side of the same runs. A wrong-but-mapped span hashes the wrong bytes, so the cache can hand back a wrong vertex buffer: corrupt draws. That is a candidate single explanation for run 9's flicker and for run 8's red/blue channel split in the hideout (same ungated, no-profiler configuration; runs 6 and 7 with the profiler on did not show visual corruption, consistent with timing sensitivity). What is NOT established: where the bad index data originates. Candidates are torn or stale stream data under fast_gpu (snapshot taken at pass start while the title races ahead and reuses the ring) and the backend reading live arrays the title is rewriting. The crash site is proven; the data's provenance is not.

Decisive test, queued, needs NO rebuild: add the line RECOMP_GL_VCACHE=0 to options.txt (vc_on() honours it; the cache is on by default; RECOMP_GL_VCACHE_MB sets its budget, 64 MB on GLES) and run title, load, pick, dance. If the dance survives and the flicker stops, the vertex cache path is confirmed as the crash site. There is also a RECOMP_GL_VCACHE_VERIFY debug switch in the same file if you want the cache to check itself.

Candidate fix if the test confirms: in vc_content or vc_prepare, clamp each hashed span to the array's real extent (the guest RAM backing), and skip the cache for that draw (fall back to the old path) when lo/hi run past it. That converts the crash class into at worst a cache miss. Whether to also attack the provenance question is a design call for you and Brandon; the clamp is the cheap, local, clearly-safe half.

## Other open threads

- Red/blue channel split (run 8's first hideout visit, on the permanent build): red and blue copies of the scene offset to either side, appeared after some running around, intermittent, absent in run 8's second attempt. No screenshot yet. If it recurs, the useful observations are whether the HUD is split too, whether it flickers, and whether it covers the whole screen. See the theory above before treating it as its own bug.
- Widescreen boost ghosting (parked): during a boost, a darker 4:3-shaped square sits inside the widescreen frame and the boost distortion looks exaggerated (Brandon's caveat: possibly the low-fps environment). Suspect: the fullscreen effect quad renders/samples at native 640x480 (logs show [CURTAIN] full-screen quad 640x480) while the main render is stretched wide. Cheap test: boost with widescreen=0. JSRF_BOOST_DUMP frame captures exist for this. Practical note: the hideout has no paint cans to build boost charge; Dogenzaka is the boost test area.
- Run 6's character select freeze: unexplained, see the cluster section. If it recurs, a run with JSRF_SPIN_LOG=1 will capture the fatal call, at profiler-like cost.
- fast_gpu stays OFF by default. Tonight's runs are encouraging but the dance crash says the soak is not done. Separate decision, later.
- Perf polish is its own thread: title slowness at start, occasional skips in gameplay. Correctness first.
- Parked product questions: whether widescreen becomes the Switch default; free camera (later, maybe).

## Tonight's commits, in order

- f10de665: JSRF_SPIN_LOG instrumentation in the make-space wrapper.
- 4f9d78b9: v2 wrap guard (large distances, gated).
- 8525ccd5: walk jump search bound 0x20000 to 0x100000, [SPIN] label fix.
- 50310ef1: v3 wrap rule for any distance, per-call [SPIN] logging (diagnostic build).
- 281fcc51: wrap rule permanent and ungated, poll back to throttled, docs/switch-testing-notes.md added.
- 4e60e714: pb_walk_sub hop guard, run 8 outcome appended to the notes doc. Current HEAD and current build on Brandon's card.

Every push was verified byte-identical against the live repo file afterwards. Host gcc -fsyntax-only passed for each changed C file; the full Switch build only happens on Brandon's LOQ, so "tested" for the code changes means the hardware runs listed above.
