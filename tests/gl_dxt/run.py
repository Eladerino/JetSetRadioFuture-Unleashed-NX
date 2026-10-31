"""DXT textures handed to the OpenGL backend as they are (tex_new_dxt), on a
headless EGL context: test_main.c with nv2a_gfx_gl.c as the engine builds it,
for OpenGL 3.3 core; once as the engine runs, once with RECOMP_GL_DXT=1 (which
hands them over on a software renderer too).

Linux, with EGL and Mesa (llvmpipe is enough): libegl-dev, libgl-dev. Skips
where there is no EGL context to be had.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
sources = ["tests/gl_dxt/test_main.c", "src/nv2a_gl/nv2a_gfx_gl.c", "src/nv2a_gl/nv2a_smaa.c",
           "src/nv2a_gl/nv2a_shader_cache.c", "src/nv2a_gl/nv2a_uniforms.c", "src/nv2a_gl/nv2a_vsh.c"]
cc = sys.argv[1] if len(sys.argv) > 1 else "cc"
with tempfile.TemporaryDirectory(prefix="gl-dxt-") as directory:
    exe = str(pathlib.Path(directory) / "test")
    subprocess.run([cc, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Isrc/nv2a_gl"] + sources
                   + ["-o", exe, "-lEGL", "-lGL", "-lm"], cwd=root, check=True)
    failed = 0
    for force in (False, True):
        env = {k: v for k, v in os.environ.items() if k != "RECOMP_GL_DXT"}
        if force:
            env["RECOMP_GL_DXT"] = "1"
        print("---", "RECOMP_GL_DXT=1" if force else "as the engine runs")
        failed += subprocess.run([exe], timeout=300, env=env).returncode != 0
sys.exit(1 if failed else 0)
