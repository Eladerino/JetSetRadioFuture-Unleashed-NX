"""The combiner constants as uniforms (nv2a_psh_consts.h).

The shaders: src/nv2a_gl/nv2a_psh.c as it is (test_emit.c), for GLSL and
Metal, and again under RECOMP_GL_OLD_OCW: the same source through new
constants, and every constant read where pshC[] holds it. The GLSL it wrote,
desktop and OpenGL ES, goes to glslangValidator where there is one.

The front half: src/nv2a_gl/nv2a_gl.c whole (test_gl.c includes it), with no
surface, so it never starts and no GL call is made. Its GL and the rest of
the engine are left out and their names left unresolved: anything that
reached one would crash, and the test would fail. The shader cache and the
device are stand-ins in test_gl.c. The program key must stay the same
through new constants, the shader cache must be handed one state for them,
and the constants must reach the program when they change.

Linux, with the EGL and GL headers (libegl-dev, libgl-dev); requires GCC on
PATH. (After spriteice's test.)
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

if not sys.platform.startswith("linux"):
    print("psh_consts: skipped, Linux only")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[2]
here = root / "tests/psh_consts"
gl_dir = root / "src/nv2a_gl"

with tempfile.TemporaryDirectory(prefix="psh-consts-") as directory:
    tmp = pathlib.Path(directory)
    validator = shutil.which("glslangValidator")
    for dialect, defines in (("desktop", []), ("es", ["-DNV2A_GLES"])):
        exe = str(tmp / ("emit-" + dialect))
        subprocess.run(["gcc", "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter", *defines,
                        "-I", str(gl_dir), str(here / "test_emit.c"), str(gl_dir / "nv2a_psh.c"),
                        str(gl_dir / "nv2a_uniforms.c"), str(gl_dir / "nv2a_vsh.c"), "-o", exe, "-lm"],
                       check=True)
        for old in (False, True):
            env = {k: v for k, v in os.environ.items() if k != "RECOMP_GL_OLD_OCW"}
            if old:
                env["RECOMP_GL_OLD_OCW"] = "1"
            frag = tmp / f"{dialect}{'-old' if old else ''}.frag"
            run = subprocess.run([exe, "old" if old else "new", str(frag)], env=env,
                                 capture_output=True, text=True, timeout=120)
            print(f"[{dialect}] " + (run.stdout + run.stderr).strip().replace("\n", f"\n[{dialect}] "))
            if run.returncode != 0:
                raise SystemExit(f"psh_consts emit: exit {run.returncode}")
            if validator:
                check = subprocess.run([validator, str(frag)], capture_output=True, text=True, timeout=60)
                if check.returncode != 0:
                    raise SystemExit(f"psh_consts: glslangValidator refused {frag.name}\n{check.stdout}")
                print(f"[{dialect}] glslangValidator: {frag.name} compiles")
    if not validator:
        print("psh_consts: no glslangValidator here, the GLSL not compiled")

    exe = str(tmp / "gl")
    subprocess.run(["gcc", "-O2", "-std=gnu11", "-w", "-no-pie", "-I", str(gl_dir),
                    "-I", str(root / "src/d3d"), str(here / "test_gl.c"), "-o", exe,
                    "-Wl,--unresolved-symbols=ignore-all", "-lm"], check=True)
    run = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    print(run.stdout.strip())
    if run.returncode != 0:
        raise SystemExit(f"psh_consts: exit {run.returncode}\n{run.stderr}")
print("psh_consts: all cases pass")
