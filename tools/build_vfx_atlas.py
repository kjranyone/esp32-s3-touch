"""Generate cel-shaded combat VFX sprite atlas + C header (`src/generated/fx_combat_atlas.h`).

Run: python tools/build_vfx_atlas.py
Requires Pillow.

Design follows Sakurai's VFX principles (#016, #035, #056, #130, #177, #199, #242):
- Never rely on muddy additive glow alone: every sprite uses crisp cel-shaded
  shapes with a dark navy outer border (#0A0E1E) + pure white hot core so it
  reads sharply over BOTH bright flashes and dark backgrounds.
- Canonical right-facing orientation (dir > 0) for slashes/cones/dust so the
  runtime horizontal flip handles left-facing attacks automatically.
- Per-sprite (w, h, ax, ay) metadata + row-span encoding matching actor atlases.
"""
from pathlib import Path
import json
import math
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SS = 4  # 4x supersampling for crisp anti-aliased cel outlines

# Canonical palette colors (RGBA)
OUTLINE = (12, 16, 34, 255)
WHITE = (255, 255, 255, 255)
AQUA_LT = (190, 252, 255, 255)
AQUA = (92, 224, 245, 255)
AQUA_DK = (36, 148, 204, 255)
VIOLET = (168, 112, 255, 255)
VIOLET_DK = (92, 52, 168, 255)
GOLD_LT = (255, 248, 176, 255)
GOLD = (255, 196, 56, 255)
ORANGE = (242, 112, 34, 255)
CRIMSON = (232, 48, 86, 255)
CRIMSON_DK = (124, 22, 62, 255)
SMOKE_LT = (132, 126, 166, 235)
SMOKE_DK = (58, 52, 86, 240)
DUST_LT = (232, 248, 246, 245)
DUST_SH = (164, 206, 206, 230)
PINK = (255, 118, 164, 255)
PINK_LT = (255, 210, 228, 255)
MINT = (112, 240, 176, 255)


def new_hi(w, h):
    return Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))


def add_outline(img, px=2, color=OUTLINE):
    """Add a crisp dark contrast outline around non-transparent shapes."""
    alpha = img.getchannel('A')
    radius = max(3, int(px * SS) | 1)
    dilated = alpha.filter(ImageFilter.MaxFilter(radius))
    # Harden semi-transparent rim slightly so outline reads crisply at 1x
    rim = dilated.point(lambda a: 255 if a >= 80 else int(a * 3))
    bg = Image.new('RGBA', img.size, color[:3] + (0,))
    bg.putalpha(rim)
    return Image.alpha_composite(bg, img)


def finish_sprite(hi, w, h, outline_px=1.75):
    if outline_px > 0:
        hi = add_outline(hi, outline_px)
    return hi.resize((w, h), Image.Resampling.LANCZOS)


def star_pts(cx, cy, r_out, r_in, n=4, rot_deg=0.0):
    pts = []
    for i in range(n * 2):
        ang = math.radians(rot_deg + i * 180.0 / n)
        r = r_out if (i % 2 == 0) else r_in
        pts.append((cx + math.cos(ang) * r, cy + math.sin(ang) * r))
    return pts


def poly(d, pts, fill):
    d.polygon([(p[0] * SS, p[1] * SS) for p in pts], fill=fill)


def ellipse(d, cx, cy, rx, ry, fill):
    d.ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=fill)


def line_seg(d, x0, y0, x1, y1, width, fill):
    d.line([(x0 * SS, y0 * SS), (x1 * SS, y1 * SS)], fill=fill, width=max(1, int(width * SS)))


def diamond(d, cx, cy, rx, ry, fill, rot_deg=0.0):
    ang = math.radians(rot_deg)
    ca, sa = math.cos(ang), math.sin(ang)
    pts = []
    for px, py in [(0, -ry), (rx, 0), (0, ry), (-rx, 0)]:
        pts.append((cx + px * ca - py * sa, cy + px * sa + py * ca))
    poly(d, pts, fill)


def crescent_pts(cx, cy, r_out, r_in, a0_deg, a1_deg, steps=22, skew_y=1.0, offset_in=(0.0, 0.0)):
    outer = []
    inner = []
    for i in range(steps + 1):
        t = i / steps
        a = math.radians(a0_deg + (a1_deg - a0_deg) * t)
        # Taper smoothly at ends
        outer.append((cx + math.cos(a) * r_out, cy + math.sin(a) * r_out * skew_y))
    for i in range(steps, -1, -1):
        t = i / steps
        a = math.radians(a0_deg + (a1_deg - a0_deg) * t)
        taper = math.sin(math.pi * t)
        r = r_out - (r_out - r_in) * (0.25 + 0.75 * taper)
        inner.append((cx + offset_in[0] + math.cos(a) * r,
                      cy + offset_in[1] + math.sin(a) * r * skew_y))
    return outer + inner


# ---------------- Sprite Generators ----------------

def gen_lumi_jab(frame):
    w, h = 72, 72
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cy = 36
    if frame == 0:
        # Tight high-velocity punch lance
        poly(d, [(10, cy - 11), (62, cy - 2), (66, cy), (62, cy + 2), (10, cy + 11), (18, cy)], AQUA)
        poly(d, [(14, cy - 6), (58, cy - 1), (62, cy), (58, cy + 1), (14, cy + 6), (20, cy)], WHITE)
        ellipse(d, 22, cy, 4, 15, AQUA_LT)
        ellipse(d, 23, cy, 2, 11, WHITE)
    elif frame == 1:
        # Expanding impact lance + piercing star + shock ring
        poly(d, [(12, cy - 15), (68, cy - 3), (70, cy), (68, cy + 3), (12, cy + 15), (24, cy)], AQUA_DK)
        poly(d, [(16, cy - 10), (65, cy - 2), (68, cy), (65, cy + 2), (16, cy + 10), (26, cy)], AQUA)
        poly(d, [(20, cy - 5), (62, cy), (20, cy + 5), (28, cy)], WHITE)
        poly(d, star_pts(56, cy, 13, 3.5, 4, 0), AQUA_LT)
        poly(d, star_pts(56, cy, 8, 2, 4, 0), WHITE)
        line_seg(d, 8, cy - 14, 40, cy - 9, 2.2, AQUA_LT)
        line_seg(d, 10, cy + 14, 44, cy + 8, 2.2, WHITE)
    else:
        # Dissipating speed streaks + shards
        line_seg(d, 18, cy - 10, 58, cy - 4, 2.5, AQUA)
        line_seg(d, 24, cy, 67, cy, 3.0, WHITE)
        line_seg(d, 16, cy + 10, 54, cy + 5, 2.5, AQUA)
        diamond(d, 60, cy - 11, 5, 3, AQUA_LT, -18)
        diamond(d, 63, cy + 10, 4.5, 2.5, WHITE, 20)
    return finish_sprite(hi, w, h), (16, 36)


def gen_lumi_slash(frame):
    w, h = 84, 84
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 24, 42
    if frame == 0:
        pts = crescent_pts(cx, cy, 50, 34, -68, 36, skew_y=0.92, offset_in=(-4, -2))
        poly(d, pts, AQUA)
        pts_w = crescent_pts(cx + 2, cy, 48, 39, -62, 32, skew_y=0.92, offset_in=(-3, -1))
        poly(d, pts_w, WHITE)
    elif frame == 1:
        pts_o = crescent_pts(cx, cy, 55, 24, -78, 72, skew_y=0.95, offset_in=(-6, 0))
        poly(d, pts_o, AQUA_DK)
        pts_m = crescent_pts(cx + 1, cy, 53, 30, -75, 68, skew_y=0.95, offset_in=(-5, 0))
        poly(d, pts_m, AQUA)
        pts_i = crescent_pts(cx + 3, cy, 51, 38, -70, 62, skew_y=0.95, offset_in=(-3, 0))
        poly(d, pts_i, WHITE)
        diamond(d, 66, 20, 6, 3, AQUA_LT, 35)
        diamond(d, 72, 54, 5, 2.5, WHITE, -25)
    else:
        pts_a = crescent_pts(cx + 2, cy, 54, 42, -52, 74, skew_y=0.95, offset_in=(-4, 1))
        poly(d, pts_a, AQUA)
        pts_b = crescent_pts(cx + 4, cy, 53, 46, -32, 66, skew_y=0.95, offset_in=(-2, 1))
        poly(d, pts_b, WHITE)
        diamond(d, 58, 14, 5, 2.5, AQUA_LT, 25)
        diamond(d, 74, 42, 6, 2.8, WHITE, 0)
        diamond(d, 56, 72, 4.5, 2.5, AQUA, -35)
    return finish_sprite(hi, w, h), (24, 42)


def gen_lumi_smash(frame):
    w, h = 96, 96
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 26, 48
    if frame == 0:
        pts = crescent_pts(cx, cy, 54, 34, -88, 12, skew_y=0.92, offset_in=(-5, -2))
        poly(d, pts, GOLD)
        pts2 = crescent_pts(cx + 2, cy, 52, 40, -84, 8, skew_y=0.92, offset_in=(-4, -1))
        poly(d, pts2, WHITE)
        poly(d, star_pts(60, 18, 11, 3, 4, 15), GOLD_LT)
    elif frame == 1:
        pts_o = crescent_pts(cx, cy, 59, 24, -84, 78, skew_y=0.88, offset_in=(-8, 0))
        poly(d, pts_o, AQUA_DK)
        pts_m = crescent_pts(cx + 2, cy, 57, 31, -80, 74, skew_y=0.88, offset_in=(-6, 0))
        poly(d, pts_m, AQUA)
        pts_g = crescent_pts(cx + 4, cy, 55, 39, -76, 70, skew_y=0.88, offset_in=(-4, 0))
        poly(d, pts_g, GOLD_LT)
        pts_w = crescent_pts(cx + 5, cy, 54, 44, -70, 64, skew_y=0.88, offset_in=(-3, 0))
        poly(d, pts_w, WHITE)
        poly(d, star_pts(72, 48, 14, 4, 4, 45), GOLD_LT)
        poly(d, star_pts(72, 48, 8, 2.5, 4, 45), WHITE)
        diamond(d, 62, 18, 6.5, 3.2, AQUA_LT, 40)
        diamond(d, 60, 78, 6.5, 3.2, AQUA_LT, -40)
    else:
        pts_a = crescent_pts(cx + 4, cy, 58, 44, -58, 80, skew_y=0.88, offset_in=(-5, 1))
        poly(d, pts_a, AQUA)
        pts_b = crescent_pts(cx + 6, cy, 57, 49, -38, 75, skew_y=0.88, offset_in=(-3, 1))
        poly(d, pts_b, WHITE)
        diamond(d, 74, 26, 6, 3, GOLD_LT, 30)
        diamond(d, 80, 50, 7, 3.2, AQUA_LT, 5)
        diamond(d, 68, 76, 6, 3, WHITE, -35)
    return finish_sprite(hi, w, h), (26, 48)


def gen_heavy_arc(frame):
    w, h = 96, 96
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 28, 48
    if frame == 0:
        pts = crescent_pts(cx, cy, 56, 34, -85, 20, skew_y=0.95, offset_in=(-5, 0))
        poly(d, pts, ORANGE)
        pts2 = crescent_pts(cx + 2, cy, 53, 40, -80, 15, skew_y=0.95, offset_in=(-4, 0))
        poly(d, pts2, GOLD_LT)
    elif frame == 1:
        pts_o = crescent_pts(cx, cy, 62, 22, -82, 78, skew_y=0.95, offset_in=(-7, 0))
        poly(d, pts_o, ORANGE)
        pts_m = crescent_pts(cx + 2, cy, 60, 30, -78, 74, skew_y=0.95, offset_in=(-5, 0))
        poly(d, pts_m, GOLD)
        pts_i = crescent_pts(cx + 4, cy, 58, 39, -72, 68, skew_y=0.95, offset_in=(-3, 0))
        poly(d, pts_i, WHITE)
        poly(d, [(72, 42), (82, 36), (78, 48), (68, 49)], GOLD)
        poly(d, [(64, 70), (74, 76), (66, 82), (58, 75)], ORANGE)
    else:
        pts = crescent_pts(cx + 4, cy, 62, 48, -45, 82, skew_y=0.95, offset_in=(-4, 2))
        poly(d, pts, GOLD)
        poly(d, [(78, 34), (86, 30), (83, 40)], GOLD_LT)
        poly(d, [(76, 68), (85, 72), (76, 78)], ORANGE)
        diamond(d, 80, 52, 6, 3, WHITE, 10)
    return finish_sprite(hi, w, h), (28, 48)


def gen_boss_lash(frame):
    w, h = 112, 96
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 30, 48
    if frame == 0:
        pts = crescent_pts(cx, cy, 62, 42, -72, 22, skew_y=0.76, offset_in=(-5, 0))
        poly(d, pts, VIOLET_DK)
        pts2 = crescent_pts(cx + 2, cy, 60, 48, -68, 16, skew_y=0.76, offset_in=(-4, 0))
        poly(d, pts2, CRIMSON)
    elif frame == 1:
        pts_o = crescent_pts(cx, cy, 68, 28, -74, 72, skew_y=0.76, offset_in=(-8, 0))
        poly(d, pts_o, CRIMSON_DK)
        pts_m = crescent_pts(cx + 2, cy, 66, 36, -70, 68, skew_y=0.76, offset_in=(-6, 0))
        poly(d, pts_m, CRIMSON)
        pts_v = crescent_pts(cx + 4, cy, 64, 44, -64, 62, skew_y=0.76, offset_in=(-4, 0))
        poly(d, pts_v, VIOLET)
        pts_w = crescent_pts(cx + 6, cy, 62, 51, -56, 54, skew_y=0.76, offset_in=(-2, 0))
        poly(d, pts_w, PINK_LT)
        # Wicked thorns along outer edge
        for ang_d in (-40, -10, 18, 45):
            a = math.radians(ang_d)
            bx = cx + math.cos(a) * 63
            by = cy + math.sin(a) * 63 * 0.76
            tx = cx + math.cos(a - 0.14) * 75
            ty = cy + math.sin(a - 0.14) * 75 * 0.76
            nx, ny = -math.sin(a) * 4, math.cos(a) * 4
            poly(d, [(bx - nx, by - ny), (tx, ty), (bx + nx, by + ny)], CRIMSON)
    else:
        pts = crescent_pts(cx + 4, cy, 67, 52, -40, 72, skew_y=0.76, offset_in=(-4, 1))
        poly(d, pts, VIOLET)
        diamond(d, 86, 28, 7, 2.8, CRIMSON, 32)
        diamond(d, 94, 48, 8, 3.0, PINK_LT, 4)
        diamond(d, 82, 68, 6.5, 2.8, CRIMSON, -30)
    return finish_sprite(hi, w, h), (30, 48)


def gen_hit_light(frame):
    w, h = 56, 56
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 28, 28
    if frame == 0:
        poly(d, star_pts(cx, cy, 24, 5, 4, 12), AQUA)
        poly(d, star_pts(cx, cy, 17, 3.5, 4, 12), WHITE)
        ellipse(d, cx, cy, 7, 7, WHITE)
    elif frame == 1:
        ellipse(d, cx, cy, 19, 19, AQUA)
        ellipse(d, cx, cy, 14, 14, (0, 0, 0, 0))
        # Punch a clear inner cutout using a temporary layer? ImageDraw ellipse alpha=0 replaces RGBA!
        # Let's redraw cleanly: ring via outer circle then erase center via mask
        hi = new_hi(w, h)
        d = ImageDraw.Draw(hi)
        ellipse(d, cx, cy, 18, 18, AQUA_LT)
        ellipse(d, cx, cy, 13, 13, (0, 0, 0, 0))
        poly(d, star_pts(cx, cy, 22, 4, 4, 45), WHITE)
        diamond(d, cx - 18, cy - 12, 5, 2.2, AQUA_LT, 35)
        diamond(d, cx + 19, cy + 10, 5, 2.2, WHITE, 30)
        diamond(d, cx + 14, cy - 16, 4.5, 2.0, AQUA, -48)
    elif frame == 2:
        pts_a = crescent_pts(cx, cy, 23, 18, -50, 40)
        pts_b = crescent_pts(cx, cy, 23, 18, 130, 220)
        poly(d, pts_a, AQUA)
        poly(d, pts_b, AQUA)
        diamond(d, cx + 19, cy - 16, 4.5, 2.0, WHITE, -40)
        diamond(d, cx - 20, cy + 16, 4.5, 2.0, AQUA_LT, -40)
        diamond(d, cx + 18, cy + 18, 4.0, 2.0, WHITE, 45)
        diamond(d, cx - 18, cy - 18, 4.0, 2.0, AQUA_LT, 45)
    else:
        diamond(d, cx + 21, cy - 18, 3.5, 1.8, WHITE, -40)
        diamond(d, cx - 21, cy + 18, 3.5, 1.8, AQUA_LT, -40)
        diamond(d, cx + 20, cy + 19, 3.0, 1.6, AQUA, 45)
    return finish_sprite(hi, w, h, 1.5), (28, 28)


def gen_hit_heavy(frame):
    w, h = 84, 84
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 42, 42
    if frame == 0:
        poly(d, star_pts(cx, cy, 38, 8, 6, 18), GOLD)
        poly(d, star_pts(cx, cy, 32, 6, 4, -15), AQUA_LT)
        poly(d, star_pts(cx, cy, 24, 5, 4, -15), WHITE)
        line_seg(d, 8, 66, 76, 18, 4.5, WHITE)
    elif frame == 1:
        ellipse(d, cx, cy, 31, 31, GOLD)
        ellipse(d, cx, cy, 24, 24, (0, 0, 0, 0))
        poly(d, star_pts(cx, cy, 34, 6, 4, 45), AQUA_LT)
        poly(d, star_pts(cx, cy, 22, 4, 4, 0), WHITE)
        for ang in (20, 75, 140, 200, 260, 325):
            a = math.radians(ang)
            diamond(d, cx + math.cos(a) * 32, cy + math.sin(a) * 32, 6.5, 2.5, GOLD_LT, ang)
    elif frame == 2:
        for a0, a1 in [(-35, 55), (85, 165), (200, 290)]:
            poly(d, crescent_pts(cx, cy, 36, 29, a0, a1), AQUA)
        for ang in (35, 125, 215, 305):
            a = math.radians(ang)
            diamond(d, cx + math.cos(a) * 34, cy + math.sin(a) * 34, 6, 2.4, WHITE, ang)
    else:
        for ang in (15, 105, 195, 285):
            a = math.radians(ang)
            diamond(d, cx + math.cos(a) * 35, cy + math.sin(a) * 35, 4.5, 2.0, GOLD_LT, ang)
    return finish_sprite(hi, w, h, 1.75), (42, 42)


def gen_mira_bolt(frame):
    w, h = 48, 36
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cy = 18
    phase = frame * 2.0
    # Flowing twin arcane tail ribbons
    tail_y1 = cy - 4 + math.sin(phase) * 2.5
    tail_y2 = cy + 4 - math.sin(phase) * 2.5
    poly(d, [(4, tail_y1), (26, cy - 5), (28, cy), (26, cy + 1), (8, tail_y1 + 3)], VIOLET)
    poly(d, [(6, tail_y2), (26, cy + 5), (28, cy), (26, cy - 1), (10, tail_y2 - 3)], AQUA)
    # Faceted spinning crystal core on front right
    diamond(d, 32, cy, 11, 8, AQUA_DK, frame * 30)
    diamond(d, 32, cy, 8, 5.5, AQUA_LT, frame * 30)
    diamond(d, 33, cy, 4.5, 3, WHITE, frame * 30)
    poly(d, star_pts(34, cy, 10, 2.2, 4, frame * 22), WHITE)
    return finish_sprite(hi, w, h, 1.5), (24, 18)


def gen_mira_burst(frame):
    w, h = 56, 56
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 28, 28
    if frame == 0:
        poly(d, star_pts(cx, cy, 22, 6, 6, 0), AQUA)
        poly(d, star_pts(cx, cy, 14, 4, 6, 30), WHITE)
    elif frame == 1:
        ellipse(d, cx, cy, 22, 22, AQUA)
        ellipse(d, cx, cy, 17, 17, (0, 0, 0, 0))
        poly(d, star_pts(cx, cy, 20, 4, 4, 45), VIOLET)
        poly(d, star_pts(cx, cy, 13, 3, 4, 0), WHITE)
        ellipse(d, cx - 16, cy - 12, 4, 4, AQUA_LT)
        ellipse(d, cx + 16, cy + 12, 4, 4, AQUA_LT)
    else:
        for a0, a1 in [(0, 70), (120, 190), (240, 310)]:
            poly(d, crescent_pts(cx, cy, 24, 19, a0, a1), VIOLET)
        ellipse(d, cx - 18, cy + 14, 3, 3, AQUA_LT)
        ellipse(d, cx + 18, cy - 14, 3, 3, WHITE)
    return finish_sprite(hi, w, h, 1.5), (28, 28)


def gen_shockwave(frame):
    w, h = 96, 64
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, gy = 48, 56
    if frame == 0:
        ellipse(d, cx, gy, 36, 9, GOLD)
        ellipse(d, cx, gy, 24, 5, WHITE)
        poly(d, [(32, gy), (48, 20), (64, gy)], GOLD_LT)
        poly(d, [(40, gy), (48, 28), (56, gy)], WHITE)
    elif frame == 1:
        ellipse(d, cx, gy, 44, 10, ORANGE)
        ellipse(d, cx, gy, 34, 6, (0, 0, 0, 0))
        poly(d, [(24, gy), (34, 16), (44, gy)], GOLD)
        poly(d, [(38, gy), (48, 6), (58, gy)], AQUA_LT)
        poly(d, [(52, gy), (62, 16), (72, gy)], GOLD)
        poly(d, [(43, gy), (48, 14), (53, gy)], WHITE)
    elif frame == 2:
        ellipse(d, cx, gy, 46, 9, GOLD)
        ellipse(d, cx, gy, 38, 6, (0, 0, 0, 0))
        poly(d, [(18, gy - 4), (24, 24), (30, gy - 4)], GOLD)
        poly(d, [(42, gy - 6), (48, 12), (54, gy - 6)], WHITE)
        poly(d, [(66, gy - 4), (72, 24), (78, gy - 4)], GOLD)
        poly(d, [(12, gy - 14), (17, gy - 20), (21, gy - 12)], ORANGE)
        poly(d, [(78, gy - 14), (84, gy - 20), (86, gy - 10)], ORANGE)
    else:
        for a0, a1 in [(-20, 60), (120, 200)]:
            poly(d, crescent_pts(cx, gy, 44, 38, a0, a1, skew_y=0.25), GOLD_LT)
        diamond(d, 30, 22, 4, 8, AQUA_LT, -12)
        diamond(d, 48, 12, 4, 10, WHITE, 0)
        diamond(d, 66, 22, 4, 8, GOLD_LT, 12)
    return finish_sprite(hi, w, h, 1.6), (48, 56)


def gen_explode(frame):
    """Sakurai #056: Flash (0) -> Fireball Blast (1-2) -> Dark Smoke & Embers (3-4) -> Dissipate (5)."""
    w, h = 96, 96
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 48, 48
    if frame == 0:
        # 閃光: Black-and-white high-contrast blinding star flash
        poly(d, star_pts(cx, cy, 44, 9, 8, 10), GOLD_LT)
        poly(d, star_pts(cx, cy, 38, 7, 4, 0), WHITE)
        ellipse(d, cx, cy, 14, 14, OUTLINE)
        ellipse(d, cx, cy, 9, 9, WHITE)
    elif frame == 1:
        # 爆炎1: Intense cel fireball lobes
        for dx, dy, r in [(0, 0, 30), (-16, -8, 18), (18, -6, 17), (-10, 16, 18), (14, 14, 17)]:
            ellipse(d, cx + dx, cy + dy, r, r, ORANGE)
        for dx, dy, r in [(0, -2, 23), (-12, -5, 13), (13, -4, 12), (8, 10, 12)]:
            ellipse(d, cx + dx, cy + dy, r, r, GOLD)
        ellipse(d, cx - 2, cy - 2, 14, 14, AQUA_LT)
        ellipse(d, cx - 3, cy - 3, 9, 9, WHITE)
    elif frame == 2:
        # 爆炎2: Peak purification blast + emerging dark smoke edge
        for dx, dy, r in [(-22, -14, 16), (22, -12, 16), (-20, 16, 15), (20, 16, 15), (0, -24, 16)]:
            ellipse(d, cx + dx, cy + dy, r, r, SMOKE_DK)
        for dx, dy, r in [(0, 0, 26), (-14, -8, 15), (14, -6, 15), (-10, 12, 14), (12, 10, 14)]:
            ellipse(d, cx + dx, cy + dy, r, r, GOLD)
        ellipse(d, cx, cy, 17, 17, AQUA_LT)
        ellipse(d, cx, cy, 10, 10, WHITE)
        poly(d, star_pts(cx - 26, cy - 22, 7, 2, 4, 20), WHITE)
        poly(d, star_pts(cx + 28, cy + 20, 7, 2, 4, 40), GOLD_LT)
    elif frame == 3:
        # 黒煙1: Billowing dark smoke overtakes core + bright sparks
        for dx, dy, r in [(-22, -16, 17), (20, -18, 16), (-24, 14, 16), (22, 14, 17), (0, -24, 18), (0, 20, 16)]:
            ellipse(d, cx + dx, cy + dy, r, r, SMOKE_DK)
        for dx, dy, r in [(-12, -10, 12), (12, -12, 11), (10, 10, 11)]:
            ellipse(d, cx + dx, cy + dy, r, r, SMOKE_LT)
        ellipse(d, cx, cy, 11, 11, (0, 0, 0, 0))
        diamond(d, cx - 28, cy - 28, 5, 2.5, GOLD_LT, -40)
        diamond(d, cx + 30, cy - 24, 5, 2.5, AQUA_LT, 35)
        diamond(d, cx + 26, cy + 28, 5, 2.5, GOLD, -30)
    elif frame == 4:
        # 黒煙2・残り火
        for dx, dy, r in [(-24, -18, 13), (24, -18, 12), (-24, 18, 12), (24, 16, 13), (0, -28, 12)]:
            ellipse(d, cx + dx, cy + dy, r, r, SMOKE_DK)
        for dx, dy, r in [(-26, -20, 7), (22, -20, 7)]:
            ellipse(d, cx + dx, cy + dy, r, r, SMOKE_LT)
        poly(d, star_pts(cx - 12, cy + 6, 7, 2, 4, 0), GOLD_LT)
        poly(d, star_pts(cx + 18, cy - 6, 6, 1.8, 4, 45), AQUA_LT)
    else:
        # 収束: Organic asymmetric dissipating smoke puffs
        for dx, dy, r in [(-28, -18, 9), (22, -26, 6), (-18, 24, 6), (28, 16, 8), (6, -32, 5)]:
            ellipse(d, cx + dx, cy + dy, r, int(r * 0.85), SMOKE_LT)
    return finish_sprite(hi, w, h, 1.75), (48, 48)


def gen_dust(frame):
    w, h = 40, 28
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    if frame == 0:
        ellipse(d, 22, 20, 11, 7, DUST_SH)
        ellipse(d, 14, 18, 8, 6, DUST_LT)
        ellipse(d, 24, 17, 9, 5, WHITE)
    elif frame == 1:
        ellipse(d, 18, 17, 10, 7, DUST_SH)
        ellipse(d, 10, 13, 7, 5, DUST_LT)
        ellipse(d, 28, 19, 6, 4, DUST_LT)
        ellipse(d, 16, 14, 7, 4, WHITE)
    else:
        ellipse(d, 11, 13, 5, 4, DUST_LT)
        ellipse(d, 24, 16, 4, 3, DUST_SH)
    return finish_sprite(hi, w, h, 1.3), (20, 24)


def gen_crystal_gem(frame):
    w, h = 24, 24
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 12, 12
    # Faceted diamond crystal
    poly(d, [(cx, 3), (20, cy), (cx, 21), (4, cy)], AQUA_DK)
    poly(d, [(cx, 3), (20, cy), (cx, cy), (4, cy)], AQUA)
    poly(d, [(cx, 3), (cx, cy), (4, cy)], AQUA_LT)
    poly(d, [(cx, 5), (15, cy - 2), (cx, cy), (8, cy - 1)], WHITE)
    if frame == 1:
        poly(d, star_pts(cx - 4, 6, 5.5, 1.5, 4, 0), WHITE)
        poly(d, star_pts(18, 17, 4.0, 1.2, 4, 45), AQUA_LT)
    return finish_sprite(hi, w, h, 1.25), (12, 12)


def gen_confetti(frame):
    w, h = 20, 20
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    cx, cy = 10, 10
    if frame == 0:
        poly(d, star_pts(cx, cy, 8, 3.6, 5, -18), GOLD)
        poly(d, star_pts(cx - 1, cy - 1, 4.5, 2.0, 5, -18), GOLD_LT)
    elif frame == 1:
        diamond(d, cx, cy, 5.5, 7.5, AQUA, 22)
        diamond(d, cx - 1, cy - 1, 3.0, 4.5, WHITE, 22)
    elif frame == 2:
        poly(d, [(cx, 3), (16, 7), (14, 16), (6, 15), (4, 8)], PINK)
        poly(d, [(cx, 4), (14, 7), (10, 11), (6, 8)], PINK_LT)
    else:
        poly(d, [(4, 6), (15, 4), (16, 13), (5, 15)], MINT)
        poly(d, [(5, 7), (14, 5), (14, 9), (5, 11)], WHITE)
    return finish_sprite(hi, w, h, 1.15), (10, 10)


def gen_heart_buff():
    w, h = 24, 24
    hi = new_hi(w, h)
    d = ImageDraw.Draw(hi)
    ellipse(d, 8, 9, 5, 5, PINK)
    ellipse(d, 16, 9, 5, 5, PINK)
    poly(d, [(3, 10), (21, 10), (12, 20)], PINK)
    ellipse(d, 7, 8, 2.2, 1.8, WHITE)
    return finish_sprite(hi, w, h, 1.25), (12, 12)


def build_vfx_atlas():
    specs = []
    for f in range(3): specs.append((f'LUMI_JAB_{f}', *gen_lumi_jab(f)))
    for f in range(3): specs.append((f'LUMI_SLASH_{f}', *gen_lumi_slash(f)))
    for f in range(3): specs.append((f'LUMI_SMASH_{f}', *gen_lumi_smash(f)))
    for f in range(3): specs.append((f'HEAVY_ARC_{f}', *gen_heavy_arc(f)))
    for f in range(3): specs.append((f'BOSS_LASH_{f}', *gen_boss_lash(f)))
    for f in range(4): specs.append((f'HIT_LIGHT_{f}', *gen_hit_light(f)))
    for f in range(4): specs.append((f'HIT_HEAVY_{f}', *gen_hit_heavy(f)))
    for f in range(3): specs.append((f'MIRA_BOLT_{f}', *gen_mira_bolt(f)))
    for f in range(3): specs.append((f'MIRA_BURST_{f}', *gen_mira_burst(f)))
    for f in range(4): specs.append((f'SHOCKWAVE_{f}', *gen_shockwave(f)))
    for f in range(6): specs.append((f'EXPLODE_{f}', *gen_explode(f)))
    for f in range(3): specs.append((f'DUST_{f}', *gen_dust(f)))
    for f in range(2): specs.append((f'CRYSTAL_GEM_{f}', *gen_crystal_gem(f)))
    for f in range(4): specs.append((f'CONFETTI_{f}', *gen_confetti(f)))
    specs.append(('HEART_BUFF', *gen_heart_buff()))

    # Pack into single 256-color palette for fast quantization & crisp colors
    max_w = max(im.width for _, im, _ in specs)
    max_h = max(im.height for _, im, _ in specs)
    cols = 8
    rows = (len(specs) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * max_w, rows * max_h), (0, 0, 0, 0))
    for i, (_, im, _) in enumerate(specs):
        sheet.paste(im, ((i % cols) * max_w, (i // cols) * max_h))
    sheet.save(ROOT / 'assets_src' / 'fx_combat_atlas.png')

    indexed = sheet.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    palette = indexed.getpalette('RGBA')
    colors, alpha = [], []
    for i in range(256):
        r, g, b, a = palette[i * 4:i * 4 + 4]
        colors.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        alpha.append(255 if a >= 238 else 0 if a <= 10 else a)
        palette[i * 4 + 3] = alpha[-1]
    indexed.putpalette(palette, rawmode='RGBA')
    quantized_rgba = indexed.convert('RGBA')

    # Save contact preview on checkerboard/split light+dark strip so contrast on BOTH is verified
    preview = Image.new('RGBA', (cols * max_w, rows * max_h), (24, 28, 56, 255))
    pd = ImageDraw.Draw(preview)
    for r_idx in range(rows):
        for c_idx in range(cols):
            x0, y0 = c_idx * max_w, r_idx * max_h
            # Bottom half light strip to verify dark outlines on bright flashes
            pd.rectangle([x0, y0 + max_h // 2, x0 + max_w - 1, y0 + max_h - 1], fill=(224, 232, 240, 255))
            pd.rectangle([x0, y0, x0 + max_w - 1, y0 + max_h - 1], outline=(70, 80, 110, 255))
    preview.alpha_composite(quantized_rgba)
    previews_dir = ROOT / 'assets_src' / 'previews'
    previews_dir.mkdir(exist_ok=True)
    preview.save(previews_dir / 'fx_contact.png')

    # Save animated preview GIF showing key VFX sequences
    gif_frames = []
    demo_indices = [
        0, 1, 2,           # Lumi Jab
        3, 4, 5,           # Lumi Slash
        6, 7, 8,           # Lumi Smash
        12, 13, 14,        # Boss Lash
        19, 20, 21, 22,    # Hit Heavy
        29, 30, 31, 32,    # Shockwave
        33, 34, 35, 36, 37, 38,  # Explosion (#056)
    ]
    for idx in demo_indices:
        _, im, (ax, ay) = specs[idx]
        qim = quantized_rgba.crop(((idx % cols) * max_w, (idx // cols) * max_h,
                                   (idx % cols) * max_w + im.width, (idx // cols) * max_h + im.height))
        bg = Image.new('RGBA', (128, 128), (28, 36, 56, 255))
        bg.alpha_composite(qim, (64 - ax, 64 - ay))
        gif_frames.append(bg.convert('RGB').resize((256, 256), Image.Resampling.NEAREST))
    gif_frames[0].save(previews_dir / 'fx_preview.gif', save_all=True,
                       append_images=gif_frames[1:], duration=100, loop=0)

    # Encode spans
    spans, meta = [], []
    for idx, (name, im, (ax, ay)) in enumerate(specs):
        x0, y0 = (idx % cols) * max_w, (idx // cols) * max_h
        crop_idx = indexed.crop((x0, y0, x0 + im.width, y0 + im.height))
        px_data = list(crop_idx.get_flattened_data() if hasattr(crop_idx, 'get_flattened_data') else crop_idx.getdata())
        w, h = im.size
        off = len(spans)
        for y in range(h):
            x = 0
            while x < w:
                p = px_data[y * w + x]
                if alpha[p] == 0:
                    x += 1
                    continue
                start = x
                opaque = alpha[p] == 255
                run = []
                while x < w:
                    p = px_data[y * w + x]
                    if not alpha[p] or (alpha[p] == 255) != opaque:
                        break
                    run.append(colors[p])
                    if not opaque:
                        run.append(alpha[p])
                    x += 1
                spans.extend([y, start, x - start, 255 if opaque else 0] + run)
        meta.append((name, w, h, ax, ay, off, len(spans) - off))

    out_h = ROOT / 'src' / 'generated' / 'fx_combat_atlas.h'
    out_h.parent.mkdir(exist_ok=True)
    lines = [
        '// Generated by tools/build_vfx_atlas.py',
        '#pragma once',
        '#include <stdint.h>',
        'namespace vfx_sprites {',
        'enum Id : uint8_t {',
    ]
    for i, (name, *_rest) in enumerate(meta):
        lines.append(f'    FXS_{name} = {i},')
    lines.append(f'    FXS_COUNT = {len(meta)}')
    lines.append('};')
    lines.append('struct Meta { uint16_t w, h; int16_t ax, ay; uint32_t offset; uint32_t words; };')
    lines.append('static const Meta META[] = {')
    for name, w, h, ax, ay, off, words in meta:
        lines.append(f'    {{{w}, {h}, {ax}, {ay}, {off}u, {words}u}}, // FXS_{name}')
    lines.append('};')
    lines.append('static const uint16_t SPANS[] = {')
    for i in range(0, len(spans), 32):
        lines.append('    ' + ','.join(map(str, spans[i:i + 32])) + ',')
    lines.append('};')
    lines.append('} // namespace vfx_sprites')
    out_h.write_text('\n'.join(lines) + '\n', encoding='utf-8')

    summary = dict(count=len(meta), total_bytes=len(spans) * 2 + len(meta) * 16)
    print(json.dumps(summary, indent=2))
    return summary


if __name__ == '__main__':
    build_vfx_atlas()
