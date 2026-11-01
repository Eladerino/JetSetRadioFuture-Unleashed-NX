# Switch port: status and conventions

This fork brings Jet Set Radio Future Unleashed (a static
recompilation of the Xbox original) to Nintendo Switch. It boots
and plays on real hardware; current work is performance and
stability. Testing detail lives in docs/switch-testing-notes.md.

## Repository rules

- The game image, the generated C (port/recomp/gen), build
  outputs (.nro/.elf) and anything derived from the game's binary
  never enter the repository and never leave the build machine.
  Test evidence that crosses machines is logs and screenshots.
- Upstream src/ stays free of external dependencies. Switch
  behaviour is gated by __SWITCH__ or by a runtime option.
- Test settings live in options.txt, not in ad-hoc environment
  changes, so a run can be reproduced from its log alone.
- Commit messages state what changed and what was actually
  tested. A change verified only by compilation or emulation
  says so; hardware results are quoted with their route and
  clock settings.

## Conventions

- Files inherited from upstream keep their original commentary,
  unedited. Explanatory comments added by this fork describe the
  mechanism and the evidence for it, without reference to
  individual sessions or contributors; where a fork comment sits
  inside an upstream file, its subject (a Switch behaviour, a
  hardware measurement, a guard) makes the provenance clear.
- READMEs and docs are short summaries of what things do now,
  not changelogs. History lives in the commit log.

## Current state

- fast_gpu is the default on Switch (see the testing notes for
  the machinery and its guards). The serial path remains
  available with fast_gpu=0.
- An in-game menu (hold Minus) carries the display, graphics
  and controller settings; most apply live, and choices are
  remembered between runs.
- The renderer drops, and logs, any draw whose index range runs
  outside guest memory, instead of faulting in the vertex cache.
- The lifted code keeps guest registers in host registers
  (RECOMP_FAST_MEM; JSRF_SLOW_MEM=ON reverts to the TLS path).
- The vertex and texture caches hash content with the aarch64
  CRC32 instructions where available
  (RECOMP_GL_CRC32HASH=0 reverts).
- The GL backend skips small uniforms whose program already holds
  the value (RECOMP_GL_UNIFORM_SKIP=0 reverts).
- The GL device runs on its own thread by default on Switch
  (RECOMP_GL_THREAD), shipped in v0.2.0. fast_gpu above is a
  separate switch and also stays on by default.
- Upstream v0.6.1 is merged: in widescreen the HUD sits at the
  screen's edges and the boost's heat haze covers the whole
  screen. Both shipped in v0.2.0.
- Performance on the reference clocks (CPU 1963 / GPU 1152 /
  mem 2366 MHz, v0.2.0 release measurements): the garage holds
  the 60 fps cap, Shibuya Terminal and Dogenzaka Hill sit near
  60 with their busiest views around 50, Chuo Street around 57,
  and 99th Street, the heaviest area so far, around 49 with
  dips into the low 30s in its densest views. At stock clocks
  the same areas run slower.
- Open items: an intermittent red/blue channel split last seen
  before draw validation landed; the draw-distance census
  (RECOMP_GL_DEPTHLOG) run over 99th Street and Shibuya that
  decides the shorter draw distance idea; and the GL worker
  thread measurement (RECOMP_GL_SPLIT) that prices Mesa's share
  of a heavy frame. Detail in the testing notes.
