#!/usr/bin/env python3
# Generates the OBSBOT4Linux app icon: the coral OBSBOT ring glyph
# (ring + centre dot + soft glow) on an obsidian rounded square, matching the
# Penelope console design tokens. Renders PNGs at several sizes.
import os
from PIL import Image, ImageDraw, ImageFilter

BG      = (18, 21, 27, 255)     # slightly-lifted obsidian so it reads on dark + light shells
BG_EDGE = (42, 49, 60, 255)     # hairline
CORAL   = (224, 108, 117, 255)  # --accent  #e06c75
CORAL_SOFT = (234, 139, 146, 255)

OUT = os.path.dirname(os.path.abspath(__file__)) + "/icons"
os.makedirs(OUT, exist_ok=True)

def render(size):
    S = 4  # supersample for crisp edges
    px = size * S
    img = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # rounded-square background
    radius = int(px * 0.22)
    d.rounded_rectangle([0, 0, px - 1, px - 1], radius=radius, fill=BG, outline=BG_EDGE, width=max(1, S))

    cx = cy = px / 2

    # soft coral glow behind the ring
    glow = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gr = px * 0.34
    gd.ellipse([cx - gr, cy - gr, cx + gr, cy + gr], fill=(224, 108, 117, 120))
    glow = glow.filter(ImageFilter.GaussianBlur(px * 0.06))
    img = Image.alpha_composite(img, glow)
    d = ImageDraw.Draw(img)

    # coral ring (annulus): outer coral disc, punch obsidian hole
    ro = px * 0.30           # outer radius
    thickness = px * 0.085
    ri = ro - thickness
    d.ellipse([cx - ro, cy - ro, cx + ro, cy + ro], fill=CORAL)
    d.ellipse([cx - ri, cy - ri, cx + ri, cy + ri], fill=BG)

    # centre dot
    dot = px * 0.085
    d.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=CORAL_SOFT)

    return img.resize((size, size), Image.LANCZOS)

for s in (512, 256, 128, 64, 48, 32, 16):
    render(s).save(f"{OUT}/obsbot4linux-{s}.png")

# canonical icon used by the .desktop / AppImage
render(256).save(f"{OUT}/obsbot4linux.png")
print("wrote icons to", OUT)
