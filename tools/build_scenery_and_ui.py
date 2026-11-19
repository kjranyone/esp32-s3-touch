"""Build seamless painted scenery, midground parallax props, and cut-in portraits.

Run: python tools/build_scenery_and_ui.py
Requires Pillow.

Generates:
- assets_src/bg_crystal_sky.png, bg_crystal_ground.png, cutin_*.png (if missing, imports from artifact dir)
- assets_src/previews/scenery_preview.png (2x wrap seam check + props)
- assets_src/previews/cutins_preview.png (cut-ins on dark banners to check alpha keying)
- src/generated/scenery_assets.h (256-color RGB565 sky/ground strips + span-encoded props & cut-ins)
"""
from collections import deque
from pathlib import Path
import json
import math
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets_src'
PREVIEWS = ASSETS / 'previews'
ARTIFACT_DIR = Path(r'C:\Users\kojiro\.gemini\antigravity-cli\brain\aa43be39-c586-4c05-bb28-efd3b8cba508')

ARTIFACT_SOURCES = {
    'bg_crystal_sky.png': (ARTIFACT_DIR / 'bg_crystal_sky_v2_1794974870510.jpg', ()),
    'bg_crystal_ground.png': (ARTIFACT_DIR / 'bg_crystal_ground_final_1794974886363.jpg', ()),
    'cutin_boss.png': (ARTIFACT_DIR / 'cutin_boss_closeup_1794974874289.jpg', [(0.366, 0.832)]),
    'cutin_lumi.png': (ARTIFACT_DIR / 'cutin_lumi_v2_1794974877960.jpg', [(0.248, 0.820)]),
    'cutin_mira.png': (ARTIFACT_DIR / 'cutin_mira_1794974858149.jpg', []),
    'cutin_goro.png': (ARTIFACT_DIR / 'cutin_goro_1794974862117.jpg', []),
    'cutin_lumi_win.png': (ARTIFACT_DIR / 'cutin_lumi_win_1794974860198.jpg', [(0.701, 0.656)]),
}

SS = 4


def get_pixels_list(im):
    return list(im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata())


def key_white_background(img, extra_seeds=()):
    """Remove exterior white background via border flood-fill + explicit exterior pocket seeds."""
    rgba = img.convert('RGBA')
    w, h = rgba.size
    px = rgba.load()

    def is_bg_candidate(r, g, b):
        mx, mn = max(r, g, b), min(r, g, b)
        return mn >= 242 and (mx - mn) <= 12

    mask_bg = [[False] * w for _ in range(h)]
    q = deque()

    def try_seed(x, y):
        if 0 <= x < w and 0 <= y < h and not mask_bg[y][x]:
            r, g, b, _ = px[x, y]
            if is_bg_candidate(r, g, b):
                mask_bg[y][x] = True
                q.append((x, y))

    for x in range(w):
        try_seed(x, 0)
        try_seed(x, h - 1)
    for y in range(h):
        try_seed(0, y)
        try_seed(w - 1, y)
    for fx, fy in extra_seeds:
        try_seed(int(fx * w), int(fy * h))

    while q:
        x, y = q.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h and not mask_bg[ny][nx]:
                r, g, b, _ = px[nx, ny]
                if is_bg_candidate(r, g, b):
                    mask_bg[ny][nx] = True
                    q.append((nx, ny))

    # Apply alpha & defringe 1px boundary pixels to eliminate bright white halos
    for y in range(h):
        for x in range(w):
            if mask_bg[y][x]:
                px[x, y] = (0, 0, 0, 0)

    border_adj = []
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            if mask_bg[y][x]:
                continue
            if mask_bg[y - 1][x] or mask_bg[y + 1][x] or mask_bg[y][x - 1] or mask_bg[y][x + 1]:
                r, g, b, a = px[x, y]
                mn, mx = min(r, g, b), max(r, g, b)
                if mn >= 185 and (mx - mn) <= 32:
                    # Soft translucent rim darkened toward navy lineart so no white fringe shows
                    lum = (r + g + b) / 3.0
                    na = max(0, min(255, int((252.0 - lum) * 3.6)))
                    border_adj.append((x, y, (int(r * 0.65), int(g * 0.65), int(b * 0.72), na)))
    for x, y, col in border_adj:
        px[x, y] = col

    return rgba


def ensure_assets_src():
    ASSETS.mkdir(exist_ok=True)
    for dst_name, (src_path, seeds) in ARTIFACT_SOURCES.items():
        dst_path = ASSETS / dst_name
        if not src_path.exists():
            if dst_path.exists():
                continue
            raise FileNotFoundError(f'Missing source image: {src_path}')
        raw = Image.open(src_path)
        if dst_name.startswith('cutin_'):
            rgba = key_white_background(raw, seeds)
            rgba.save(dst_path)
        else:
            raw.convert('RGB').save(dst_path)


def make_seamless_strip(img, width, height, blend_w=64):
    """Resize to (width + blend_w, height) and cosine-crossfade edges into a seamless loop."""
    scaled = img.convert('RGB').resize((width + blend_w, height), Image.Resampling.LANCZOS)
    src = scaled.load()
    out = Image.new('RGB', (width, height))
    dst = out.load()
    for y in range(height):
        for x in range(width):
            if x < blend_w:
                t = 0.5 - 0.5 * math.cos(math.pi * (x / blend_w))
                r0, g0, b0 = src[width + x, y]
                r1, g1, b1 = src[x, y]
                dst[x, y] = (
                    int(r0 * (1.0 - t) + r1 * t),
                    int(g0 * (1.0 - t) + g1 * t),
                    int(b0 * (1.0 - t) + b1 * t),
                )
            else:
                dst[x, y] = src[x, y]
    return out


def quantize_strip(img):
    indexed = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    pal_raw = indexed.getpalette('RGB')
    pal565 = []
    for i in range(256):
        r, g, b = pal_raw[i * 3:i * 3 + 3]
        pal565.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return pal565, get_pixels_list(indexed), indexed.convert('RGB')


# ---------------- Cel-shaded Midground Parallax Props ----------------

def poly(d, pts, fill):
    d.polygon([(p[0] * SS, p[1] * SS) for p in pts], fill=fill)


def ellipse(d, cx, cy, rx, ry, fill):
    d.ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=fill)


def rect(d, x0, y0, x1, y1, fill):
    d.rectangle([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=fill)


def prop_outline_and_resize(hi, w, h, outline_col=(48, 56, 86, 255)):
    alpha = hi.getchannel('A')
    dilated = alpha.filter(ImageFilter.MaxFilter(7))
    rim = dilated.point(lambda a: 255 if a >= 80 else int(a * 3))
    bg = Image.new('RGBA', hi.size, outline_col[:3] + (0,))
    bg.putalpha(rim)
    comp = Image.alpha_composite(bg, hi).resize((w, h), Image.Resampling.LANCZOS)
    # Apply subtle atmospheric twilight haze (Sakurai #010/#251: midground stays softer than characters)
    px = comp.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            # Blend 24% toward misty twilight indigo-aqua
            r = int(r * 0.76 + 104 * 0.24)
            g = int(g * 0.76 + 132 * 0.24)
            b = int(b * 0.76 + 168 * 0.24)
            px[x, y] = (r, g, b, a)
    return comp


def gen_prop_arch():
    """Ruined ancient sanctuary arch with inlaid glowing aqua crystal."""
    w, h = 96, 88
    hi = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(hi)
    ST_DK = (54, 68, 96, 255)
    ST_MD = (76, 94, 126, 255)
    ST_LT = (106, 128, 158, 255)
    MOSS = (54, 108, 96, 255)
    CR_DK = (52, 168, 198, 255)
    CR_LT = (146, 238, 246, 255)
    # Left & right weathered pillars
    rect(d, 16, 28, 32, 86, ST_MD)
    rect(d, 16, 28, 22, 86, ST_LT)
    rect(d, 27, 28, 32, 86, ST_DK)
    rect(d, 64, 28, 80, 86, ST_MD)
    rect(d, 64, 28, 70, 86, ST_LT)
    rect(d, 75, 28, 80, 86, ST_DK)
    # Top lintel beam (slightly weathered/broken right corner)
    poly(d, [(10, 18), (84, 18), (88, 28), (12, 28)], ST_MD)
    poly(d, [(10, 18), (84, 18), (84, 22), (10, 22)], ST_LT)
    poly(d, [(12, 26), (86, 26), (88, 29), (12, 29)], ST_DK)
    # Pediments / capitals
    rect(d, 14, 28, 34, 33, ST_LT)
    rect(d, 62, 28, 82, 33, ST_LT)
    # Moss patches
    poly(d, [(14, 18), (38, 18), (34, 24), (16, 25)], MOSS)
    poly(d, [(64, 48), (74, 52), (70, 62), (64, 60)], MOSS)
    # Glowing crystal spire tucked into right pillar & top center keystone
    poly(d, [(48, 6), (55, 22), (48, 28), (41, 22)], CR_DK)
    poly(d, [(48, 6), (48, 28), (41, 22)], CR_LT)
    poly(d, [(78, 36), (88, 54), (80, 74), (70, 56)], CR_DK)
    poly(d, [(78, 36), (80, 74), (70, 56)], CR_LT)
    return prop_outline_and_resize(hi, w, h), (48, 88)


def gen_prop_crystal():
    """Cluster of serene translucent aqua sanctuary crystals."""
    w, h = 64, 64
    hi = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(hi)
    CR_DK = (48, 148, 184, 255)
    CR_MD = (82, 198, 222, 255)
    CR_LT = (164, 242, 250, 255)
    MOSS = (48, 96, 86, 255)
    # Side shards
    poly(d, [(14, 22), (26, 40), (22, 62), (10, 48)], CR_DK)
    poly(d, [(14, 22), (18, 56), (10, 48)], CR_MD)
    poly(d, [(48, 24), (56, 46), (46, 62), (38, 44)], CR_DK)
    poly(d, [(48, 24), (44, 56), (38, 44)], CR_MD)
    # Main central crystal spire
    poly(d, [(32, 6), (45, 30), (40, 62), (24, 62), (19, 30)], CR_MD)
    poly(d, [(32, 6), (32, 62), (24, 62), (19, 30)], CR_LT)
    # Mossy base stone
    ellipse(d, 32, 60, 22, 6, MOSS)
    return prop_outline_and_resize(hi, w, h), (32, 64)


def gen_prop_lantern():
    """Carved mossy stone obelisk housing an aqua crystal lantern."""
    w, h = 48, 76
    hi = Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(hi)
    ST_DK = (52, 64, 92, 255)
    ST_MD = (74, 92, 122, 255)
    ST_LT = (108, 130, 158, 255)
    CR_LT = (176, 250, 255, 255)
    CR_MD = (86, 214, 238, 255)
    # Pillar shaft
    rect(d, 17, 36, 31, 74, ST_MD)
    rect(d, 17, 36, 21, 74, ST_LT)
    rect(d, 27, 36, 31, 74, ST_DK)
    # Lantern chamber & glowing crystal core
    rect(d, 13, 20, 35, 36, ST_DK)
    poly(d, [(24, 22), (30, 28), (24, 34), (18, 28)], CR_MD)
    poly(d, [(24, 22), (24, 34), (18, 28)], CR_LT)
    # Side stone frames of lantern
    rect(d, 13, 20, 17, 36, ST_LT)
    rect(d, 31, 20, 35, 36, ST_MD)
    # Pagoda-style cap
    poly(d, [(24, 8), (39, 20), (9, 20)], ST_MD)
    poly(d, [(24, 8), (24, 20), (9, 20)], ST_LT)
    return prop_outline_and_resize(hi, w, h), (24, 76)


def fit_cutin(img, box_size, align_bottom=True):
    rgba = img.convert('RGBA')
    bbox = rgba.getchannel('A').point(lambda a: 255 if a >= 24 else 0).getbbox()
    if bbox:
        rgba = rgba.crop(bbox)
    ratio = min(box_size / rgba.width, box_size / rgba.height)
    nw = max(1, round(rgba.width * ratio))
    nh = max(1, round(rgba.height * ratio))
    resized = rgba.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new('RGBA', (box_size, box_size), (0, 0, 0, 0))
    ox = (box_size - nw) // 2
    oy = (box_size - nh) if align_bottom else (box_size - nh) // 2
    canvas.paste(resized, (ox, oy))
    return canvas


def encode_span_sprites(items):
    """Pack multiple RGBA sprites into one 256-color palette & generate span stream."""
    max_w = max(im.width for _, im, _ in items)
    max_h = max(im.height for _, im, _ in items)
    sheet = Image.new('RGBA', (len(items) * max_w, max_h), (0, 0, 0, 0))
    for i, (_, im, _) in enumerate(items):
        sheet.paste(im, (i * max_w, 0))
    indexed = sheet.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    pal = indexed.getpalette('RGBA')
    colors, alpha = [], []
    for i in range(256):
        r, g, b, a = pal[i * 4:i * 4 + 4]
        colors.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        alpha.append(255 if a >= 236 else 0 if a <= 12 else a)
        pal[i * 4 + 3] = alpha[-1]
    indexed.putpalette(pal, rawmode='RGBA')
    q_rgba = indexed.convert('RGBA')

    spans, meta = [], []
    cropped_q = []
    for i, (name, im, (ax, ay)) in enumerate(items):
        w, h = im.size
        crop_idx = indexed.crop((i * max_w, 0, i * max_w + w, h))
        cropped_q.append((name, q_rgba.crop((i * max_w, 0, i * max_w + w, h))))
        px_data = get_pixels_list(crop_idx)
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
    return meta, spans, cropped_q


def build():
    ensure_assets_src()
    PREVIEWS.mkdir(exist_ok=True)

    sky_src = Image.open(ASSETS / 'bg_crystal_sky.png')
    gnd_raw = Image.open(ASSETS / 'bg_crystal_ground.png')
    # Crop horizontally between the two matching balustrade pillars (~24.6% .. 75.7%) for natural tile alignment
    gw, gh = gnd_raw.size
    gnd_src = gnd_raw.crop((int(gw * 0.246), 0, int(gw * 0.757), gh))
    sky_strip = make_seamless_strip(sky_src, 512, 96, blend_w=48)
    gnd_strip = make_seamless_strip(gnd_src, 512, 256, blend_w=16)

    sky_pal, sky_idx, sky_q = quantize_strip(sky_strip)
    gnd_pal, gnd_idx, gnd_q = quantize_strip(gnd_strip)

    # Props + Cut-ins
    prop_arch, a_arch = gen_prop_arch()
    prop_cry, a_cry = gen_prop_crystal()
    prop_lan, a_lan = gen_prop_lantern()

    cutin_boss = fit_cutin(Image.open(ASSETS / 'cutin_boss.png'), 112, align_bottom=True)
    cutin_lumi = fit_cutin(Image.open(ASSETS / 'cutin_lumi.png'), 84, align_bottom=True)
    cutin_mira = fit_cutin(Image.open(ASSETS / 'cutin_mira.png'), 84, align_bottom=True)
    cutin_goro = fit_cutin(Image.open(ASSETS / 'cutin_goro.png'), 84, align_bottom=True)
    cutin_win = fit_cutin(Image.open(ASSETS / 'cutin_lumi_win.png'), 84, align_bottom=True)

    span_items = [
        ('PROP_ARCH', prop_arch, a_arch),
        ('PROP_CRYSTAL', prop_cry, a_cry),
        ('PROP_LANTERN', prop_lan, a_lan),
        ('CUTIN_BOSS', cutin_boss, (56, 56)),
        ('CUTIN_LUMI', cutin_lumi, (42, 42)),
        ('CUTIN_MIRA', cutin_mira, (42, 42)),
        ('CUTIN_GORO', cutin_goro, (42, 42)),
        ('CUTIN_LUMI_WIN', cutin_win, (42, 42)),
    ]
    meta, spans, cropped_q = encode_span_sprites(span_items)

    # Preview 1: 960x352 2-tile seamless scenery panorama with midground parallax props
    pano = Image.new('RGBA', (960, 352), (16, 20, 40, 255))
    for x0 in (0, 512):
        pano.paste(sky_q.convert('RGBA'), (x0, 0))
    # Composite midground props tucked just behind balustrade top (y = 102)
    props_map = {n: im for n, im in cropped_q[:3]}
    for px, pkey, py in [(90, 'PROP_ARCH', 102), (240, 'PROP_CRYSTAL', 103),
                         (370, 'PROP_LANTERN', 102), (550, 'PROP_CRYSTAL', 103),
                         (710, 'PROP_ARCH', 102), (860, 'PROP_LANTERN', 102)]:
        pim = props_map[pkey]
        pano.alpha_composite(pim, (px - pim.width // 2, py - pim.height))
    for x0 in (0, 512):
        pano.paste(gnd_q.convert('RGBA'), (x0, 96))
    pd = ImageDraw.Draw(pano)
    # Mark wrap seam at x=512 near bottom border only as subtle tick + draw BELT_Y0/Y1 guides on right edge
    pd.line([(512, 0), (512, 16)], fill=(255, 80, 80, 255), width=2)
    pd.line([(512, 336), (512, 351)], fill=(255, 80, 80, 255), width=2)
    pano.save(PREVIEWS / 'scenery_preview.png')

    # Preview 2: Cut-ins composited on dark indigo/crimson banners to QA alpha edges
    cprev = Image.new('RGBA', (480, 320), (14, 18, 36, 255))
    cd = ImageDraw.Draw(cprev)
    cd.rectangle([0, 0, 480, 128], fill=(34, 16, 44, 255))
    cd.line([(0, 127), (480, 127)], fill=(232, 48, 86, 255), width=2)
    cprev.alpha_composite(cropped_q[3][1], (16, 12))  # Boss cut-in
    for idx, x0 in enumerate([16, 120, 224, 328]):
        cd.rectangle([x0 - 4, 156, x0 + 88, 256], fill=(18, 28, 58, 255), outline=(92, 224, 245, 255))
        cprev.alpha_composite(cropped_q[4 + idx][1], (x0, 164))
    cprev.save(PREVIEWS / 'cutins_preview.png')

    # Emit C++ header
    out_h = ROOT / 'src' / 'generated' / 'scenery_assets.h'
    out_h.parent.mkdir(exist_ok=True)
    lines = [
        '// Generated by tools/build_scenery_and_ui.py',
        '#pragma once',
        '#include <stdint.h>',
        'namespace scenery {',
        'constexpr int SKY_W = 512, SKY_H = 96;',
        'constexpr int GND_W = 512, GND_H = 256;',
    ]
    for arr_name, ctype, vals in [
        ('SKY_PAL', 'uint16_t', sky_pal),
        ('SKY_IDX', 'uint8_t', sky_idx),
        ('GND_PAL', 'uint16_t', gnd_pal),
        ('GND_IDX', 'uint8_t', gnd_idx),
    ]:
        lines.append(f'static const {ctype} {arr_name}[] = {{')
        for i in range(0, len(vals), 32):
            lines.append('    ' + ','.join(map(str, vals[i:i + 32])) + ',')
        lines.append('};')

    lines.append('enum SpriteId : uint8_t {')
    for i, (name, *_rest) in enumerate(meta):
        lines.append(f'    SC_{name} = {i},')
    lines.append(f'    SC_COUNT = {len(meta)}')
    lines.append('};')
    lines.append('struct Meta { uint16_t w, h; int16_t ax, ay; uint32_t offset; uint32_t words; };')
    lines.append('static const Meta META[] = {')
    for name, w, h, ax, ay, off, words in meta:
        lines.append(f'    {{{w}, {h}, {ax}, {ay}, {off}u, {words}u}}, // SC_{name}')
    lines.append('};')
    lines.append('static const uint16_t SPANS[] = {')
    for i in range(0, len(spans), 32):
        lines.append('    ' + ','.join(map(str, spans[i:i + 32])) + ',')
    lines.append('};')
    lines.append('} // namespace scenery')
    out_h.write_text('\n'.join(lines) + '\n', encoding='utf-8')

    summary = {
        'sky_bytes': len(sky_pal) * 2 + len(sky_idx),
        'gnd_bytes': len(gnd_pal) * 2 + len(gnd_idx),
        'span_bytes': len(spans) * 2 + len(meta) * 16,
        'sprites': [{'name': m[0], 'w': m[1], 'h': m[2], 'bytes': m[6] * 2} for m in meta],
    }
    print(json.dumps(summary, indent=2))
    return summary


if __name__ == '__main__':
    build()
