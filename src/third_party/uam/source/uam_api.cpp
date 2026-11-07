/* uam.h's C interface over DekoCompiler (jsrf-nx). */
#include "compiler_iface.h"
#include "uam.h"

#include <mutex>

namespace {
/* jsrf-nx: one compile at a time, enforced here. The GLSL front end is
 * process-global state (uam.h's own contract: "One compile at a time:
 * Mesa's GLSL front end is global state"), but nothing made callers
 * honour it: the shader-pack worker thread (dk_compile.c) compiles its
 * batch without any lock while other threads compile theirs -- the
 * boot thread builds the menu's shaders (dk_imgui_backend_init) and,
 * lazily, the post pass's (post_init). On a boot with no shader pack
 * the worker is mid-batch when the menu builds; the interleaved parses
 * corrupt each other's built-in function lookups, and a compile comes
 * back with builtins missing ("no function with name 'texture'" on
 * the menu's fragment shader) -- the menu then stays shut for the
 * whole session. Every compile in the process funnels through this
 * one function, so a lock around the whole compile makes the contract
 * true for all callers at once. The lock is a leaf: nothing under a
 * compile calls back into this interface. */
std::mutex s_compile_lock;
}  // namespace

extern "C" void *uam_compile_dksh(int stage, const char *glsl, uint32_t *size)
{
	std::lock_guard<std::mutex> lock(s_compile_lock);
	DekoCompiler::s_persistent = true;
	DekoCompiler c{stage == UAM_STAGE_FRAGMENT ? pipeline_stage_fragment : pipeline_stage_vertex};
	if (!c.CompileGlsl(glsl))
		return NULL;
	return c.OutputDkshToMemory(size);
}

extern "C" void uam_shutdown(void)
{
	std::lock_guard<std::mutex> lock(s_compile_lock);
	DekoCompiler::Shutdown();
}
