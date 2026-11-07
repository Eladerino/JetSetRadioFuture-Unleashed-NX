# Third-party licence texts

`LGPL-2.1.txt` is the verbatim GNU Lesser General Public License v2.1, from

    https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt

It applies to the files listed under "LGPL-2.1-or-later" in the top-level
NOTICE — the MCPX APU sources and `src/nv2a/nv2a_regs.h`, extracted from xemu
and remaining the copyright of espes, Jannik Vogel and Matt Borgerson.

Shipping the verbatim text alongside those files is an LGPL requirement, not
a courtesy.

`MPL-2.0.txt` is the verbatim Mozilla Public License, version 2.0. It
applies to the debug-server core — `src/nv2a_dk/debug_server.cpp` and
`src/nv2a_dk/debug_server.h`, from SwitchWakerHD by centollOS — and to the
Dear ImGui deko backend adapted from the same project
(`src/nv2a_dk/nv2a_dk_imgui_dk.cpp`). See the "MPL-2.0 — debug server core"
section of the top-level NOTICE, which also states how to obtain the
source, as MPL-2.0 §3.2 expects.

`imgui-MIT.txt` is the MIT licence of Dear ImGui by Omar Cornut. It applies
to both vendored copies of the library (`third_party/imgui` and the copy
beside the deko renderer) and is byte-identical to the `LICENSE.txt`
shipped with the library itself.

`uam-LICENSE.txt` is the licence file of UAM (the shader compiler by
fincs) exactly as it ships in `src/third_party/uam/LICENSE`. It covers UAM
itself and, reproduced inside it, the Mesa licence text for the parts of
Mesa UAM derives from.

`libnx-ISC.txt` is the ISC licence of libnx, the Nintendo Switch homebrew
library this port links against. The text is taken from the libnx
project's own repository (its LICENSE.md); the devkitPro install used
for the release build ships no licence file for libnx.

`deko3d-zlib.txt` is the zlib-style licence of deko3d by fincs, the GPU
library the renderer is built on. In the devkitPro install it is carried
in the header of `libnx/include/deko3d.h`; the text here is that header
text, verbatim.

`MIT-JSRF-Recomp.txt` is the MIT License with Miggas's copyright notice, from

    https://github.com/Miggas/JSRF-Recomp

It applies to `port/sse_p3.h`, taken from that project (see NOTICE).
