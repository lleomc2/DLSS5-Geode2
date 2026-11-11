#!/usr/bin/env python3
"""Desktop preview of the REAL mod code.

Builds src/Pipeline.cpp against a tiny Geode shim (tools/preview/shim) and runs it on an offscreen
OpenGL ES 2 context (Mesa software rendering), so what you see here is what the mod's shaders do.

    python3 tools/preview/run.py                       # sample scene, all four qualities -> preview_out/
    python3 tools/preview/run.py --theme red -q 3      # red sample scene, MAX only
    python3 tools/preview/run.py --image my_frame.png  # your own 1600x720 screenshot
    python3 tools/preview/run.py --selftest            # GL state / recovery tests

Needs: g++ (C++23), Mesa EGL + GLES2 (libegl-dev libgles-dev libgl1-mesa-dri), numpy and pillow.
"""
import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import make_scene  # noqa: E402

W, H = 1600, 720


def build(exe, extra_inc):
    cmd = ["g++", "-std=c++23", "-O1", "-I", str(HERE / "shim"), "-I", str(ROOT / "src")]
    for inc in extra_inc:
        cmd += ["-I", inc]
    cmd += [str(ROOT / "src" / "Pipeline.cpp"), str(HERE / "preview.cpp"), "-o", str(exe)]
    for libs in (["-lEGL", "-lGLESv2"], ["-l:libEGL.so.1", "-l:libGLESv2.so.2"]):
        if subprocess.run(cmd + libs).returncode == 0:
            return
    sys.exit("could not build the preview tool (are the EGL/GLES2 headers and libraries installed?)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-q", "--quality", type=int, action="append", help="0 Rendimiento, 1 Equilibrado, 2 Ultra, 3 MAX")
    ap.add_argument("--theme", choices=["blue", "red"], default="blue")
    ap.add_argument("--look", type=int, default=0, help="0 Cinematico, 1 Natural, 2 Calido, 3 Frio, 4 Neon")
    ap.add_argument("--image", help="use your own 1600x720 frame instead of the sample scene")
    ap.add_argument("--out", default="preview_out")
    ap.add_argument("--inc", action="append", default=[], help="extra include dir (EGL/GLES2 headers)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "preview"
    build(exe, a.inc)

    scene = Image.open(a.image).convert("RGB") if a.image else make_scene.build(theme=a.theme)
    if scene.size != (W, H):
        scene = scene.resize((W, H), Image.LANCZOS)
    scene.save(out / "original.png")
    raw = out / "scene.rgba"
    np.asarray(scene.convert("RGBA"))[::-1].copy().tofile(raw)

    if a.selftest:
        sys.exit(subprocess.run([str(exe), "selftest", str(raw), str(W), str(H)]).returncode)

    for q in a.quality or [0, 1, 2, 3]:
        res = out / f"q{q}.rgba"
        subprocess.run([str(exe), "render", str(raw), str(res), str(W), str(H), str(q), str(a.look), "30"], check=True)
        px = np.fromfile(res, dtype=np.uint8).reshape(H, W, 4)[::-1, :, :3]
        Image.fromarray(px).save(out / f"q{q}.png")
        res.unlink()
    raw.unlink()
    print("done ->", out.resolve())


if __name__ == "__main__":
    main()
