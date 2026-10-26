# Switch port: testing notes

How Switch testing works, what the fast-path machinery does, and the
current state of the port's known issues. Performance figures are
measured on real hardware (Atmosphère emuMMC, title takeover,
overclock CPU 1963 / GPU 1152 / mem 2133 MHz) unless stated otherwise.

## Workflow

- Build on the build machine under WSL2: `git pull`, then
  `SKIP_RECOMP=1 tools/switch/build.sh <path to game image>`. The disc
  image, the generated C (port/recomp/gen) and the .nro/.elf stay on
  that machine; only logs are shared from a test run.
- All test settings live in options.txt beside the .nro. build.sh
  writes it only when missing, so edits persist between builds.
- Run tools/switch/prof.sh over a run's log BEFORE rebuilding: it
  resolves the sampler's addresses against the .elf that matches the
  run. Crash reports print nro+ offsets; resolve them the same way.
- A useful test report is the run's log, its prof.txt / prof-pcs.txt,
  and a note of the route taken (title, save load, character select,
  which stage) and anything seen on screen.

## The fast GPU path (fast_gpu)

fast_gpu (RECOMP_NV2A_FLUSH_ACK) is on by default on Switch;
fast_gpu=0 in options.txt turns it off. It lets the title run ahead
of the GPU thread instead of serialising on every flush. Four pieces
of it are load-bearing:

- **Make-space branch choice** (port/recomp_manual.c, the
  sub_001910E0 override). D3D's spin loop is a plain unsigned compare
  with no handling for a wrapped pushbuffer ring: across a wrap
  (DMA_GET numerically ahead of DMA_PUT) the compare can never
  succeed, because GET is republished as the new, smaller PUT and the
  waiting title submits nothing more. Rule: across a wrap, always
  take the sleeping branch (raise a small answer into sleep range,
  leave a large one); on a linear ring, clamp as before. The
  sleeping branch wakes on software method 5.
- **The acknowledgement walk** (src/kernel/xbox_memory_layout.c).
  On a wrap the walk must find the ring's jump, which sits at the
  ring's end; the search bound is 1 MB of tail, and the snapshot
  windows the walk reads from are the same 1 MB, so the tail is
  never read live after GET has been released. The walk stops at
  the jump, so the bound is a cap, not a cost.
- **Subroutine validation** (src/kernel/nv2a_pb_scan.c). CALL
  targets and the jump hops followed out of a subroutine are both
  validated against the 64 MB RAM size; a target past the end of
  RAM ends the walk instead of being dereferenced.
- **Software-method delivery.** A raised method can be lost if the
  PGRAPH status word is cleared read-then-store while the timer
  thread raises another; the clears are compare-exchange, raised
  bits are re-raised if delivery stalls, and the vblank DPC
  finishes its acknowledgement from inside its own spin. A lost
  method 5 was the cause of the long make-space freezes.

Two renderer guards belong to the same story:

- **Draw validation** (src/nv2a_gl/nv2a_gl.c, do_draw_body). A draw
  whose index range runs outside guest memory is dropped and
  counted in the log ("draw N dropped: attribute ... outside guest
  memory"). Before this guard, such a draw took the vertex cache's
  content hash off the end of guest memory and faulted.
- **Vertex cache hashing.** The cache's content hash (also the
  texture cache's verify hash) folds with the aarch64 CRC32
  instructions where available; RECOMP_GL_CRC32HASH=0 restores the
  software hash. Values are compared only within a run.

## Measured state

- Title average rose from 26.5 fps (serial path) to 32.7 fps with
  the fast path and snapshot walk; light scenes reach the 60 cap.
- Bulk index runs on the GPU thread took the run average from
  44.4 to 51.2 fps. Skipping the per-open case walk on SD removed
  a similar amount of title-side file time.
- Gameplay is typically 40-60 fps; heavy districts (Shibuya
  terminal, 300-680 draws) run at a median of about 40 fps on the
  current build, where the game thread is the limit. The lifted
  code keeps guest registers in host registers (RECOMP_FAST_MEM;
  JSRF_SLOW_MEM=ON reverts), which is where the last gain came
  from: the same district measured 28-40 fps before it. The log
  line "[MANUAL] generated code: fast guest memory" confirms the
  fast path is live in a build.
- A 470-second soak on the current delivery machinery completed
  with no freeze and no crash, 2,190 software methods delivered.

## Open items

- **Red/blue channel split.** Seen twice in the hideout area on
  pre-guard builds: red and blue copies of the scene offset to
  either side, intermittent. It has not recurred since draw
  validation landed, and no dropped draws were logged in the runs
  since. If it recurs, check the same log for dropped-draw lines
  at that moment before treating it as a separate bug; a
  screenshot that shows whether the HUD splits too is the other
  useful evidence.
- **Boost blur in widescreen.** Screen-space blur quads that
  sample a rendered surface are no longer squeezed
  (RECOMP_WIDE_FX_FULL=0 restores the old behaviour). Awaiting
  visual confirmation on hardware that the boost effect is
  correct; the practical test area is Dogenzaka (the hideout
  has no paint cans to build boost charge).
- **Vertex hashing cost.** Even with CRC32 the content hash is a
  visible share of the GPU thread; whether more is worth taking
  there is a profiling question, answered by prof.sh over a
  heavy-scene run.
- fast_gpu's default-on status is recent. A longer soak across
  more stages is still the deciding evidence for keeping it.
