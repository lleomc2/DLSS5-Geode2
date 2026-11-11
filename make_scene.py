#!/usr/bin/env python3
"""Synthetic Geometry-Dash-like frame used to preview the post-processing look.
It is NOT a screenshot of the game: it only imitates its typical ingredients (flat colour
gradients, white-outlined dark blocks, spikes, saws, orbs, glow, UI text)."""
import math
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1600, 720
SS = 3  # supersampling for clean edges


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


THEMES = {
    'blue': ((34, 24, 98), (22, 96, 214), (16, 70, 190), (6, 28, 92)),
    'red': ((70, 10, 40), (210, 50, 50), (150, 30, 40), (50, 8, 24)),
}


def build(seed=7, with_text=True, theme='blue'):
    random.seed(seed)
    w, h = W * SS, H * SS
    s = SS

    # --- background: vertical gradient + faint tiled shapes -----------------------------
    ys = np.linspace(0, 1, h)[:, None, None]
    tt = THEMES[theme]
    top = np.array(tt[0], dtype=np.float32)
    bot = np.array(tt[1], dtype=np.float32)
    bg = (top * (1 - ys) + bot * ys) * np.ones((1, w, 1), dtype=np.float32)
    img = Image.fromarray(bg.astype(np.uint8), "RGB").convert("RGBA")

    bgshapes = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(bgshapes)
    for gx in range(-1, 11):
        for gy in range(-1, 6):
            cx = (gx * 190 + (95 if gy % 2 else 0)) * s
            cy = gy * 150 * s
            r = 70 * s
            pts = [(cx + r * math.cos(math.radians(60 * i + 30)), cy + r * math.sin(math.radians(60 * i + 30))) for i in range(6)]
            d.polygon(pts, outline=(255, 255, 255, 26), width=3 * s)
            d.ellipse((cx - 14 * s, cy - 14 * s, cx + 14 * s, cy + 14 * s), fill=(255, 255, 255, 14))
    img = Image.alpha_composite(img, bgshapes)

    # --- ground ---------------------------------------------------------------------------
    gy0 = 600 * s
    g = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    for y in range(gy0, h):
        t = (y - gy0) / (h - gy0)
        c = lerp(tt[2], tt[3], t)
        gd.line((0, y, w, y), fill=c + (255,))
    for x in range(0, w, 160 * s):
        gd.line((x, gy0, x, h), fill=(255, 255, 255, 18), width=2 * s)
    gd.line((0, gy0 + 40 * s, w, gy0 + 40 * s), fill=(255, 255, 255, 22), width=2 * s)
    img = Image.alpha_composite(img, g)

    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gl = ImageDraw.Draw(glow)
    gl.line((0, gy0, w, gy0), fill=(140, 230, 255, 255), width=10 * s)
    glow = glow.filter(ImageFilter.GaussianBlur(14 * s))
    img = Image.alpha_composite(img, glow)
    d = ImageDraw.Draw(img)
    d.line((0, gy0, w, gy0), fill=(255, 255, 255, 255), width=4 * s)

    # --- helpers --------------------------------------------------------------------------
    def block(x, y, size=80, accent=(80, 200, 255)):
        x, y, size = x * s, y * s, size * s
        gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(gl).rectangle((x, y, x + size, y + size), outline=accent + (200,), width=6 * s)
        gl = gl.filter(ImageFilter.GaussianBlur(7 * s))
        nonlocal img
        img = Image.alpha_composite(img, gl)
        dd = ImageDraw.Draw(img)
        dd.rectangle((x, y, x + size, y + size), fill=(8, 12, 40, 255))
        dd.rectangle((x + 6 * s, y + 6 * s, x + size - 6 * s, y + size - 6 * s), outline=(30, 56, 140, 255), width=3 * s)
        dd.rectangle((x + 20 * s, y + 20 * s, x + size - 20 * s, y + size - 20 * s), fill=(14, 22, 66, 255))
        dd.rectangle((x, y, x + size, y + size), outline=(236, 248, 255, 255), width=4 * s)

    def spike(x, y, size=70):
        x, y, size = x * s, y * s, size * s
        gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(gl).polygon([(x, y + size), (x + size / 2, y), (x + size, y + size)], outline=(255, 90, 160, 220), width=6 * s)
        gl = gl.filter(ImageFilter.GaussianBlur(6 * s))
        nonlocal img
        img = Image.alpha_composite(img, gl)
        dd = ImageDraw.Draw(img)
        dd.polygon([(x, y + size), (x + size / 2, y), (x + size, y + size)], fill=(4, 4, 16, 255), outline=(255, 255, 255, 255), width=4 * s)

    def saw(cx, cy, r=70, rot=0):
        cx, cy, r = cx * s, cy * s, r * s
        gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(gl).ellipse((cx - r, cy - r, cx + r, cy + r), outline=(255, 190, 60, 220), width=8 * s)
        gl = gl.filter(ImageFilter.GaussianBlur(8 * s))
        nonlocal img
        img = Image.alpha_composite(img, gl)
        dd = ImageDraw.Draw(img)
        pts = []
        for i in range(24):
            a = math.radians(i * 15 + rot)
            rr = r if i % 2 == 0 else r * 0.72
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        dd.polygon(pts, fill=(6, 6, 20, 255), outline=(255, 255, 255, 255), width=4 * s)
        dd.ellipse((cx - r * 0.35, cy - r * 0.35, cx + r * 0.35, cy + r * 0.35), fill=(40, 20, 6, 255), outline=(255, 210, 90, 255), width=4 * s)

    def orb(cx, cy, r=30, col=(255, 230, 70)):
        cx, cy, r = cx * s, cy * s, r * s
        gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(gl).ellipse((cx - r * 1.6, cy - r * 1.6, cx + r * 1.6, cy + r * 1.6), fill=col + (170,))
        gl = gl.filter(ImageFilter.GaussianBlur(16 * s))
        nonlocal img
        img = Image.alpha_composite(img, gl)
        dd = ImageDraw.Draw(img)
        dd.ellipse((cx - r, cy - r, cx + r, cy + r), fill=col + (255,), outline=(255, 255, 255, 255), width=4 * s)
        dd.ellipse((cx - r * 0.55, cy - r * 0.55, cx + r * 0.55, cy + r * 0.55), outline=(255, 255, 255, 230), width=3 * s)

    def portal(cx, cy, col=(255, 70, 220)):
        cx, cy = cx * s, cy * s
        gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(gl).ellipse((cx - 40 * s, cy - 110 * s, cx + 40 * s, cy + 110 * s), outline=col + (255,), width=14 * s)
        gl = gl.filter(ImageFilter.GaussianBlur(16 * s))
        nonlocal img
        img = Image.alpha_composite(img, gl)
        dd = ImageDraw.Draw(img)
        dd.ellipse((cx - 34 * s, cy - 104 * s, cx + 34 * s, cy + 104 * s), outline=col + (255,), width=10 * s)
        dd.ellipse((cx - 16 * s, cy - 84 * s, cx + 16 * s, cy + 84 * s), outline=(255, 255, 255, 255), width=4 * s)

    # --- level geometry -------------------------------------------------------------------
    for i in range(6):
        block(700 + i * 80, 520)
    for i in range(3):
        block(860 + i * 80, 440, accent=(120, 255, 180))
    block(940, 360, accent=(255, 120, 200))
    spike(380, 530)
    spike(450, 530)
    saw(1330, 440, 70, 8)
    orb(570, 370)
    portal(1480, 500)

    # pad
    dd = ImageDraw.Draw(img)
    dd.pieslice((600 * s, 584 * s, 650 * s, 616 * s), 180, 360, fill=(255, 100, 220, 255), outline=(255, 255, 255, 255), width=3 * s)

    # --- particles / trail ----------------------------------------------------------------
    part = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pd = ImageDraw.Draw(part)
    for _ in range(90):
        x = random.uniform(40, 250) * s
        y = random.uniform(500, 600) * s
        r = random.uniform(2, 7) * s
        pd.ellipse((x - r, y - r, x + r, y + r), fill=(170, 255, 120, int(random.uniform(90, 255))))
    part_glow = part.filter(ImageFilter.GaussianBlur(5 * s))
    img = Image.alpha_composite(img, part_glow)
    img = Image.alpha_composite(img, part)
    trail = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(trail)
    for i in range(40):
        a = int(220 * (1 - i / 40))
        td.rectangle(((250 - i * 6) * s, (560 - i * 0.3) * s, (256 - i * 6) * s, (580 + i * 0.3) * s), fill=(190, 255, 120, a))
    img = Image.alpha_composite(img, trail.filter(ImageFilter.GaussianBlur(3 * s)))

    # --- player cube ----------------------------------------------------------------------
    px, py, ps = 250 * s, 520 * s, 80 * s
    gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(gl).rectangle((px, py, px + ps, py + ps), outline=(160, 255, 90, 230), width=8 * s)
    gl = gl.filter(ImageFilter.GaussianBlur(10 * s))
    img = Image.alpha_composite(img, gl)
    dd = ImageDraw.Draw(img)
    dd.rectangle((px, py, px + ps, py + ps), fill=(170, 255, 60, 255), outline=(0, 0, 0, 255), width=5 * s)
    dd.rectangle((px + 12 * s, py + 12 * s, px + ps - 12 * s, py + ps - 12 * s), fill=(60, 230, 255, 255), outline=(0, 0, 0, 255), width=4 * s)
    dd.rectangle((px + 26 * s, py + 26 * s, px + 38 * s, py + 42 * s), fill=(0, 0, 0, 255))
    dd.rectangle((px + 46 * s, py + 26 * s, px + 58 * s, py + 42 * s), fill=(0, 0, 0, 255))
    dd.rectangle((px + 26 * s, py + 52 * s, px + 58 * s, py + 58 * s), fill=(0, 0, 0, 255))

    # --- UI (progress bar, text) ----------------------------------------------------------
    ui = img.resize((W, H), Image.LANCZOS)
    u = ImageDraw.Draw(ui)
    u.rounded_rectangle((400, 22, 1200, 40), radius=8, fill=(0, 0, 0, 120), outline=(255, 255, 255, 220), width=2)
    u.rounded_rectangle((403, 25, 403 + int(794 * 0.37), 37), radius=6, fill=(120, 255, 90, 255))
    u.text((1220, 20), "37%", font=font(26), fill=(255, 255, 255, 255))
    if with_text:
        u.text((90, 300), "Attempt 12", font=font(46), fill=(255, 255, 255, 235))
        u.text((90, 360), "DLSS 5 preview scene", font=font(22), fill=(210, 230, 255, 220))
    return ui.convert("RGB")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "scene.png"
    theme = "red" if "--red" in sys.argv else "blue"
    build(with_text="--notext" not in sys.argv, theme=theme).save(out)
    print("saved", out)
