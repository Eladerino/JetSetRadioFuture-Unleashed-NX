# NV2A Deko3D backend

The Switch build uses this renderer by default (`XBOXRECOMP_DEKO3D=ON`). It
connects the existing NV2A pushbuffer scanner to Deko3D, the Switch's native
GPU API. The previous GLES renderer remains available with
`DEKO3D=OFF tools/switch/build.sh ...` or `-DXBOXRECOMP_DEKO3D=OFF`.

Build from WSL with `tools/switch/build.sh /path/to/game.iso` or a folder
containing `default.xbe`. The script generates game code from the user's own
disc on the build machine; generated code and game data are not part of this
source tree. A Switch build requires the devkitPro Switch toolchain,
including deko3d.

## In-game menu

`NV2A_DK_MENU` is on by default for the Deko3D Switch build. Minus opens and
closes it; the d-pad moves, the right face button (printed A) selects, the
bottom face button (printed B) backs out, and the shoulder buttons change tabs.
Display, Cheats and Music each have a separate tab and a help pane. Display
changes render scale and the frame-rate counter. Session cheats include spray
cans and coordinate teleport; the 14 free-skate destinations use the upstream
selector IDs and ask before changing maps. Music chooses a whole DJ mix for the
next stream open. These settings live for the current run. Set
`-DNV2A_DK_MENU=OFF` to omit the controller menu.

The renderer and shader-pack implementation are adapted from the MIT-licensed
`xboxrecomp` runtime in [skeammer/jsrf-nx](https://github.com/skeammer/jsrf-nx),
at commit `0014daf973c413e723dc6c0ea2ad76f1c4eb1f31`. The vendored uam shader
compiler keeps its separate license in `src/third_party/uam/LICENSE`.

## Shaders

- **Ubershaders** (`shaders/nv2a_vs.glsl`, `shaders/nv2a_fs.glsl`), compiled
  with uam when the shaders change (`shaders/build.sh`) and embedded in the
  NRO (`nv2a_dk_shaders.h`, committed). The vertex shader interprets the
  title's NV2A vertex program (decoded on the CPU, `vp_decode1`); the
  fragment shader evaluates the register combiners. They draw everything,
  slowly.
- **The shader pack** (`sdmc:/switch/jsrf-unleashed/dk_shaders.pak`): a dedicated shader
  per vertex program and per combiner setup, from the shader
  generator (`dk_shadergen.c`, through
  `dk_gen.c`) compiled with uam. Used wherever it has the setup; the
  ubershaders cover the rest. Each combiner setup is built twice: with the
  title's alpha test, and with it compiled out, which the renderer takes for
  draws with the test off -- a shader that can discard loses the GPU's early
  depth test, so every hidden pixel would run the whole combiner.

### Built on the console

The NRO builds the shader pack itself (`dk_compile.c`, with uam linked in as
a library from `src/third_party/uam`), on a thread of its own:

1. every vertex shader and pixel shader the game's XBE ships
   (`dk_xbe_scan.c`), read from `game/default.xbe`, and keys from earlier
   runs in `sdmc:/switch/jsrf-unleashed/dk_keys.bin` -- before the game starts:
   the first start shows an amber loading bar while these shaders are built.
   Later starts find them in the pack and go straight on;
   `RECOMP_DK_COMPILE_WAIT=0` lets the game start at once and builds them
   beside it;
2. while the game runs, a shader a draw needs that the pack lacks (combiner
   setups D3D builds from texture-stage states are in no file), at once --
   the ubershader draws it for the few frames that takes, then the new one
   takes over. A thin bar along the bottom of the screen shows it when more
   than a few are queued.

Each shader is appended to `dk_shaders.pak` as it is built (to the card every
16 or 5 s), so the work is done once. The pack carries a stamp of the shader
generators' sources (`dk_gen_files.txt`, `nv2a_dk_pack.h`): an update that
changes them makes the console build the pack anew, so a fixed shader bug
never lingers in an old pack. Combiner setups met in play are also recorded
to `dk_keys.bin` (`RECOMP_DK_KEYS=0` stops it). `RECOMP_DK_COMPILE=0` turns
the compiler off.

The log says what it did:

    [DK] compiler up: builds what sdmc:/switch/jsrf-unleashed/dk_shaders.pak lacks ...
    [DK] compiler: ... shaders from .../default.xbe to build
    [DK] compiler: 50 of <n> built (<t> s)
    [DK] compiler: <n> shaders built in <t> s (0 failed); <n> added to ... this run

The shader pack and keys stay on the SD card in the application's folder.
The first launch can spend extra time compiling shaders; later launches reuse
the pack. No shader pack needs to be prepared on the PC.

## Screen and internal resolution

The swapchain is made at the size the system shows the layer at: 1280x720
in the hand (or on a 720p TV), 1920x1080 docked to a 1080p TV. The renderer
asks every 30 frames (`appletGetDefaultDisplayResolution`) and makes it
again when that changes.

The title draws into surfaces larger than its own by the internal
resolution, `RECOMP_DK_SCALE`. The supplied `options.txt` starts at x1 for
performance. Use `scale=2` through `scale=4` for a fixed factor, or add
`RECOMP_DK_SCALE=auto` / `RECOMP_DK_SCALE=max` for dynamic or maximum mode:

- `max` is the screen's height: x1.5 in the hand and x2.25 docked. It uses
  more fill rate than x1, so compare frame times at your target clock settings.
- `auto` is dynamic, from x1 up to the screen's height. It starts at the top,
  steps down a quarter as soon as 30 frames average over the GPU budget
  (`RECOMP_DK_GPU_MS`, default 14 of the 16.7 ms frame), and steps up only
  after 600 frames of the game drawing (30 draws or more, so not a loading
  screen) whose average, scaled by the pixel count, still fits in 80% of the
  budget, and whose heaviest 30 frames at this level in the last 3 minutes
  would still fit the budget -- and not within 30 s of stepping down,
  doubled (up to 8 min) whenever a step up is undone within a minute. A
  change waits for the GPU and copies every surface, so it must not see-saw.
- A number fixes it (`1` is the title's own 640x480; at most 4).

The title's coordinates are untouched -- the viewport maps them onto the
larger image, and scissors, clear rectangles and sampled render targets are
converted -- so the shaders and the pack do not change. A surface the title
anti-aliases already counts its supersampling towards the factor. On a
change every surface is copied into one at the new size (depth buffers are
made anew). Fill costs grow with the pixel count.

## Switches

| | |
|---|---|
| `RECOMP_DK_SCALE=max\|auto\|<n>` | internal resolution (see above; renderer fallback is max, the supplied Switch options start at x1) |
| `RECOMP_DK_GPU_MS=<ms>` | `auto`'s GPU budget per frame (default 14) |
| `RECOMP_DK_MIPS=0` | sample level 0 only (the title's mip chains are uploaded and used by default) |
| `RECOMP_DK_TRACE=1` | surfaces, depth buffers and textures as they are made |
| `RECOMP_DK_TEX_MB=<n>` | texture memory (default 256) |
| `RECOMP_DK_TEX_EVERY=<n>` | a texture whose bytes passed 8 content checks in a row is checked every `n`-th frame instead of every frame, each on its own phase (default 4; 1 = every frame, as before). A changing one (a movie) stays at every frame; one the executor rewrote is redone at once |
| `RECOMP_DK_DATA_CACHED=0` | write the data ring (vertices, indices, texture uploads) uncached instead of using CPU-cached memory with cache cleaning before each submit |
| `RECOMP_DK_THREAD=0` | the renderer on the executor's thread, as before. By default it runs on a thread of its own behind the executor (`nv2a_backend_queue.c`): the executor decodes the pushbuffer and queues each call, the renderer's thread records them for the GPU, on two cores. `RECOMP_QUEUE_RING_KB` (8192), `RECOMP_QUEUE_FRAMES` (how many flips may be queued, 2), `RECOMP_QUEUE_SPIN_US` (50) and `RECOMP_QUEUE_PRIO` tune it. `RECOMP_QUEUE_COPY_VERTICES=0` (the default): vertex arrays read when the thread gets to the draw, with the GPU's progress held back until then (=1: copied when queued, progress passed on at once -- JSRF's D3D waits for pushbuffer room on that progress) |
| `RECOMP_DK_HITCH_MS=<ms>` | a `[hitch]` line with renderer work for every frame over this long (default 50 ms) |
| `RECOMP_DK_FRONT_FLIP=1` | swap the front-face winding |
| `RECOMP_DK_HOLE_DIAG=1` | alpha-tested pixels cyan instead of discarded |
| `RECOMP_DK_DRAWSYNC=1` | every draw submitted and waited for alone (`=<n>`: from n seconds in), so a GPU fault names the draw it stopped on; slow, a diagnostic. Use `XBOXRECOMP_DEKO3D_DEBUG=ON` for deko3d's own report |
| `RECOMP_DK_NODRAW=1` | draws counted, not drawn (phase 1) |
| `RECOMP_DK_HEARTBEAT=1` | a moving square in the corner, to tell a live screen from a hung one |
| `RECOMP_DK_FPS=1` | an FPS counter in the top-left corner: frames shown per second over the last half second, and the longest gap between two frames in that time (`MS`) |
| `RECOMP_DK_COMPILE=0` | no shader compiling on the console (the pack is only read) |
| `RECOMP_DK_COMPILE_WAIT=0` | the first start does not wait on the loading screen for the shaders; they are built beside the game |
| `RECOMP_DK_COMPRESS=0` | render targets and depth buffers without the GPU's framebuffer compression |
| `RECOMP_DK_BORDER=0` | clamp-to-border textures read transparent black instead of the title's border colour (as before) |
| `RECOMP_DK_POLY_OFFSET=0` | ignore the title's polygon offset (as before; shadows and decals then fight the surface under them) |
| `RECOMP_DK_CULL=0` | no face culling (a diagnostic) |
| `RECOMP_DK_CAPTURE=<s>` | every `<s>` seconds, one whole frame's draws written to the log (shaders, combiner key and constants, textures, blend, depth, a vertex; at most 40 frames of 300 draws) |
| `RECOMP_DK_LIGHTING=0` | no fixed-function lighting: lit draws in their vertex colour, as before (to compare) |
| `RECOMP_DK_FOG=0` | the fog unit off: no fog at all (to compare; the fog value used to be taken as the fog factor unconverted) |
| `RECOMP_DK_PIXEL_ALIGN=0` | at an internal resolution, positions are not moved onto the image's pixel centres, as before (2D quads then miss the first column and row: lines along the left and top of some cutscenes) |
| `RECOMP_DK_ZCLAMP=vertex` | depth clamped per vertex in the shaders, as before (geometry next to the camera then lost to what is behind it) |
| `RECOMP_DK_KEYS=0` | do not record new shader keys |
| `RECOMP_DK_DIR=<dir>` | where the pack and keys live (default `sdmc:/switch/jsrf-unleashed`) |

For a checking build, configure with `-DXBOXRECOMP_DEKO3D_DEBUG=ON`; deko3d's
checking library reports API misuse, with extra CPU overhead.

## Reading the log

Every two seconds, the Switch reporter prints core utilization and a
`[perf] deko3d:` line with average GPU and renderer time, draw and vertex
counts, surface/depth/texture counts, texture memory, and shaders built since
the previous report. `show_fps=1` adds the renderer's on-screen FPS counter.

Vertex-program constants are pushed by changed rows only. Each raw batch says
which rows the title wrote since the batch before (`vp_const_lo`/`hi`,
`vp_const_gen_prev`), so a draw compares those rows rather than all 192.
