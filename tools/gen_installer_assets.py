#!/usr/bin/env python3
"""GLOBUS installer artwork generator (Ninth Parallel Audio).

Creates the original icon and wizard bitmaps used by the Inno Setup installer:

    installer/assets/GLOBUS.ico          multi-size application/installer icon
    installer/assets/wizard-banner.bmp   164x314 welcome/finish side banner
    installer/assets/wizard-header.bmp   55x58 small header glyph
    installer/assets/preview_*.png       previews (not used by the installer)

All artwork is drawn from code — a globe wireframe with highlighted parallels
(the Ninth Parallel motif) on dark graphite with electric-blue / violet
accents. No third-party assets are used. Requires Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "..", "installer", "assets")

# GLOBUS palette
BG        = (14, 16, 19)
BG2       = (24, 28, 35)
PANEL     = (26, 29, 36)
TEXT      = (216, 220, 228)
DIM       = (128, 137, 155)
BLUE      = (77, 163, 255)
VIOLET    = (139, 124, 248)
TRACK     = (44, 50, 61)


def load_font(size, bold=True):
    candidates = [
        # Windows (GitHub runner)
        "C:\\Windows\\Fonts\\segoeuib.ttf" if bold else "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf" if bold else "C:\\Windows\\Fonts\\arial.ttf",
        # Linux (dev sandbox)
        f"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans{'-Bold' if bold else ''}.ttf",
        f"/usr/share/fonts/truetype/dejavu/DejaVuSans{'-Bold' if bold else ''}.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_globe(d, cx, cy, r, line=3.0, blue=BLUE, violet=VIOLET, alpha_scale=1.0):
    """Wireframe globe: outline, meridians, equator, two highlighted parallels."""
    def a(col, alpha):
        return col + (int(255 * alpha * alpha_scale),)

    w = max(1, int(line))
    # outline
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=a(blue, 1.0), width=w + 1)
    # meridians (ellipses squashed in x)
    for f, al in [(0.42, 0.85), (0.78, 0.45)]:
        d.ellipse([cx - r * f, cy - r, cx + r * f, cy + r], outline=a(blue, al), width=w)
    # equator
    d.line([cx - r, cy, cx + r, cy], fill=a(violet, 0.95), width=w)
    # highlighted parallels (the "ninth parallel" motif)
    for fy, al in [(0.5, 0.7), (-0.5, 0.7)]:
        py = cy + r * fy
        px = r * (1.0 - fy * fy) ** 0.5
        d.line([cx - px, py, cx + px, py], fill=a(violet, al), width=max(1, w - 1))
    for fy, al in [(0.82, 0.35), (-0.82, 0.35)]:
        py = cy + r * fy
        px = r * (1.0 - fy * fy) ** 0.5
        d.line([cx - px, py, cx + px, py], fill=a(blue, al), width=max(1, w - 1))


def rounded_rect(d, box, radius, fill=None, outline=None, width=1):
    d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def vertical_gradient(size, top, bottom):
    img = Image.new("RGB", size)
    px = img.load()
    w, h = size
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def make_icon():
    master = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(master)
    rounded_rect(d, [8, 8, 248, 248], 56, fill=BG + (255,))
    rounded_rect(d, [8, 8, 248, 248], 56, outline=TRACK + (255,), width=6)
    draw_globe(d, 128, 128, 82, line=9)
    # subtle glow dot at the crossing of equator and centre meridian
    d.ellipse([120, 120, 136, 136], fill=BLUE + (230,))
    d.ellipse([124, 124, 132, 132], fill=(240, 246, 255, 255))

    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    path = os.path.join(OUT, "GLOBUS.ico")
    master.save(path, format="ICO", sizes=sizes)
    master.convert("RGB").save(os.path.join(OUT, "preview_icon.png"))
    print("wrote", path)


def make_banner():
    W, H = 164, 314
    img = vertical_gradient((W, H), (10, 11, 14), (22, 26, 33))
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)

    # faint oversized globe bleeding off the bottom (depth layer)
    draw_globe(d, W // 2, H + 30, 150, line=2, alpha_scale=0.16)
    # main globe
    draw_globe(d, W // 2, 86, 52, line=3)

    # wordmark
    f_logo = load_font(26, bold=True)
    tw = d.textlength("GLOBUS", font=f_logo)
    d.text(((W - tw) / 2, 152), "GLOBUS", font=f_logo, fill=TEXT + (255,))

    # accent underline
    d.line([W / 2 - 34, 188, W / 2 + 6, 188], fill=BLUE + (255,), width=3)
    d.line([W / 2 + 6, 188, W / 2 + 34, 188], fill=VIOLET + (255,), width=3)

    f_small = load_font(9, bold=False)
    sub = "NINTH PARALLEL AUDIO"
    tw = d.textlength(sub, font=f_small)
    d.text(((W - tw) / 2, 198), sub, font=f_small, fill=DIM + (255,))

    f_desc = load_font(10, bold=False)
    for i, lineTxt in enumerate(["Polyphonic Workstation", "Synthesizer"]):
        tw = d.textlength(lineTxt, font=f_desc)
        d.text(((W - tw) / 2, 226 + i * 14), lineTxt, font=f_desc, fill=TEXT + (210,))

    f_ver = load_font(10, bold=True)
    ver = "v1.1.0"
    tw = d.textlength(ver, font=f_ver)
    rounded_rect(d, [(W - tw) / 2 - 10, 262, (W + tw) / 2 + 10, 282], 9,
                 outline=BLUE + (180,), width=1)
    d.text(((W - tw) / 2, 265), ver, font=f_ver, fill=BLUE + (255,))

    img = Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB")
    path = os.path.join(OUT, "wizard-banner.bmp")
    img.save(path, format="BMP")
    img.save(os.path.join(OUT, "preview_banner.png"))
    print("wrote", path)


def make_header():
    W, H = 55, 58
    img = vertical_gradient((W, H), BG, BG2)
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    draw_globe(d, W // 2, H // 2 - 2, 20, line=2)
    d.line([10, H - 6, 27, H - 6], fill=BLUE + (255,), width=2)
    d.line([27, H - 6, 45, H - 6], fill=VIOLET + (255,), width=2)
    img = Image.alpha_composite(img.convert("RGBA"), overlay).convert("RGB")
    path = os.path.join(OUT, "wizard-header.bmp")
    img.save(path, format="BMP")
    img.save(os.path.join(OUT, "preview_header.png"))
    print("wrote", path)


def main():
    os.makedirs(OUT, exist_ok=True)
    make_icon()
    make_banner()
    make_header()
    print("done")


if __name__ == "__main__":
    main()
