// Included inside beat namespace after Actor, AState, EType, and TALENTS are defined.
struct SpriteAtlas {
    const uint16_t* spans;
    const uint32_t* offsets;
    int size;
};
#define ATLAS(n) {sprites::n##_spans, sprites::n##_offsets, sprites::n##_size}
static const SpriteAtlas ally_atlases[] = {ATLAS(lumi), ATLAS(mira), ATLAS(goro)};
static const SpriteAtlas enemy_atlases[] = {ATLAS(slime), ATLAS(bat), ATLAS(golem), ATLAS(boss)};
#undef ATLAS
static constexpr int ACTOR_SLOTS = ALLY_N + ENEMY_N; // 11
static uint16_t* actor_pose_buf[ACTOR_SLOTS] = {};
static int16_t  cached_pose[ACTOR_SLOTS]     = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static int8_t   cached_atlas_id[ACTOR_SLOTS] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static uint32_t cached_pal_key[ACTOR_SLOTS]  = {};

static void init_actor_caches()
{
    uint32_t ally_max_w = 0;
    for (int a = 0; a < 3; ++a) {
        const SpriteAtlas& atlas = ally_atlases[a];
        for (int p = 0; p < 36; ++p) {
            uint32_t n = atlas.offsets[p + 1] - atlas.offsets[p];
            if (n > ally_max_w) ally_max_w = n;
        }
    }
    for (int i = 0; i < ALLY_N; ++i) {
        if (actor_pose_buf[i]) continue;
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) > 125000) {
            actor_pose_buf[i] = (uint16_t*)heap_caps_malloc(ally_max_w * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!actor_pose_buf[i]) {
            actor_pose_buf[i] = (uint16_t*)heap_caps_malloc(ally_max_w * 2, MALLOC_CAP_SPIRAM);
        }
        cached_pose[i] = -1;
        cached_atlas_id[i] = -1;
        cached_pal_key[i] = 0xFFFFFFFFu;
    }
    uint32_t enemy_max_w = 0;
    for (int t = 0; t < 4; ++t) {
        const SpriteAtlas& atlas = enemy_atlases[t];
        for (int p = 0; p < 36; ++p) {
            uint32_t n = atlas.offsets[p + 1] - atlas.offsets[p];
            if (n > enemy_max_w) enemy_max_w = n;
        }
    }
    for (int i = ALLY_N; i < ACTOR_SLOTS; ++i) {
        if (actor_pose_buf[i]) continue;
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) > 140000) {
            actor_pose_buf[i] = (uint16_t*)heap_caps_malloc(enemy_max_w * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!actor_pose_buf[i]) {
            actor_pose_buf[i] = (uint16_t*)heap_caps_malloc(enemy_max_w * 2, MALLOC_CAP_SPIRAM);
        }
        cached_pose[i] = -1;
        cached_atlas_id[i] = -1;
        cached_pal_key[i] = 0xFFFFFFFFu;
    }
}

static inline uint16_t remap_rgb565_pixel(uint16_t c, bool is_ally, uint8_t atlas_id,
                                          int pr1, int pg1, int pb1,
                                          int pr2, int pg2, int pb2)
{
    int r = ((c >> 11) << 3) | ((c >> 13) & 7);
    int g = (((c >> 5) & 63) << 2) | ((c >> 9) & 3);
    int b = ((c & 31) << 3) | ((c >> 2) & 7);

    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int sat = mx - mn;
    int lum = (r * 3 + g * 6 + b) / 10;

    /* 1. 輪郭線・濃影・純白ハイライトは保持 */
    if (mx < 52 || (lum < 54 && sat < 26)) return c;
    if (mn > 214 && sat < 34) return c;

    /* 2. 味方の肌色保護 (ウォームピーチ/アイボリー肌) */
    if (is_ally && r >= 150 && g >= 98 && b >= 66 && r > g && g >= b &&
        (r - b) >= 16 && (r - b) <= 118 && (r - g) <= 74 && (g - b) <= 62) {
        return c;
    }

    /* 3. 元ピクセルの色相傾向に応じて 主色(pal_pri) / 副色(pal_sec) を選択 */
    bool use_sec = false;
    if (is_ally) {
        if (atlas_id == 0 || atlas_id == 2) {
            /* LUMI / GORO: 暖色金・珊瑚差し色 → pal_sec, 青系・冷灰色 → pal_pri */
            use_sec = (r > b + 20) || (r > 180 && g > 140 && b < 110);
        } else {
            /* MIRA: 水色/緑青アクセント → pal_sec, 紫・紺・白ローブ → pal_pri */
            use_sec = (g > r + 14 && b > r + 10);
        }
    } else {
        /* 敵・ボス: 暖色目/角/棘アクセント → pal_sec, 本体装甲・外皮 → pal_pri */
        use_sec = (r > b + 24 && r > g + 8);
    }

    int tr = use_sec ? pr2 : pr1;
    int tg = use_sec ? pg2 : pg1;
    int tb = use_sec ? pb2 : pb1;

    /* 無彩色グレー(白服/金属陰)は適度に色相をブレンドして立体感を維持 */
    if (sat < 26 && !use_sec) {
        tr = (tr * 3 + r * 2) / 5;
        tg = (tg * 3 + g * 2) / 5;
        tb = (tb * 3 + b * 2) / 5;
    }

    int scale = (lum * 360) >> 8;
    int spec = (lum > 188) ? ((lum - 188) * 3) >> 2 : 0;
    int nr = ((tr * scale) >> 8) + spec;
    int ng = ((tg * scale) >> 8) + spec;
    int nb = ((tb * scale) >> 8) + spec;
    if (nr > 255) nr = 255; if (ng > 255) ng = 255; if (nb > 255) nb = 255;
    return (uint16_t)(((nr & 0xF8) << 8) | ((ng & 0xFC) << 3) | (nb >> 3));
}

static void remap_span_palette(uint16_t* buf, uint32_t words, bool is_ally, uint8_t atlas_id,
                               uint16_t pal_pri, uint16_t pal_sec)
{
    int pr1 = ((pal_pri >> 11) << 3) | ((pal_pri >> 13) & 7);
    int pg1 = (((pal_pri >> 5) & 63) << 2) | ((pal_pri >> 9) & 3);
    int pb1 = ((pal_pri & 31) << 3) | ((pal_pri >> 2) & 7);
    int pr2 = ((pal_sec >> 11) << 3) | ((pal_sec >> 13) & 7);
    int pg2 = (((pal_sec >> 5) & 63) << 2) | ((pal_sec >> 9) & 3);
    int pb2 = ((pal_sec & 31) << 3) | ((pal_sec >> 2) & 7);

    uint16_t* run = buf;
    uint16_t* end = buf + words;
    while (run + 4 <= end) {
        run += 2; /* skip y, x */
        int count = *run++;
        int opaque = *run++;
        if (opaque) {
            if (run + count > end) break;
            for (int i = 0; i < count; ++i) {
                run[i] = remap_rgb565_pixel(run[i], is_ally, atlas_id, pr1, pg1, pb1, pr2, pg2, pb2);
            }
            run += count;
        } else {
            if (run + count * 2 > end) break;
            for (int i = 0; i < count; ++i) {
                run[i * 2] = remap_rgb565_pixel(run[i * 2], is_ally, atlas_id, pr1, pg1, pb1, pr2, pg2, pb2);
            }
            run += count * 2;
        }
    }
}

static inline int floor_div256(int v)
{
    return v >= 0 ? (v >> 8) : -(((-v) + 255) >> 8);
}

/* Monotonic row-cursor alpha-blending span blitter operating inside a 32-row SRAM band buffer.
   Advances io_run across bands so each span is walked at most once per frame. */
static void blit_span_band_cursor(uint16_t* band, int by, int bh,
                                  const uint16_t*& io_run, const uint16_t* end,
                                  int w, int h, int ax, int ay,
                                  int sx, int sy, int8_t dir, uint8_t flash,
                                  int scale_x_q8 = 256, int scale_y_q8 = 256)
{
    if (io_run >= end) return;
    int by_end = by + bh;
    int dst_top = sy - ((ay * scale_y_q8 + 128) >> 8);
    int dst_h = (h * scale_y_q8 + 128) >> 8;
    if (dst_top >= by_end) return;
    if (dst_top + dst_h <= by) { io_run = end; return; }

    int reach_l = (dir > 0) ? ((ax * scale_x_q8) >> 8) + 2 : (((w - ax) * scale_x_q8) >> 8) + 2;
    int reach_r = (dir > 0) ? (((w - ax) * scale_x_q8) >> 8) + 2 : ((ax * scale_x_q8) >> 8) + 2;
    if (sx + reach_r <= 0 || sx - reach_l >= FB_W) { io_run = end; return; }

    const uint16_t flash_col = (flash == 2) ? RGB(0x18, 0x24, 0x58) : C_WHITE;
    const bool unscaled = (scale_x_q8 == 256 && scale_y_q8 == 256);
    const int inv_sx_q16 = (scale_x_q8 != 256) ? ((16777216 + (scale_x_q8 >> 1)) / scale_x_q8) : 0;

    const uint16_t* run = io_run;
    int cur_src_y = -1;
    const uint16_t* cur_row_start = run;
    const uint16_t* straddle_row_start = nullptr;

    while (run < end) {
        const uint16_t* span_hdr = run;
        int y = *run++, x = *run++, count = *run++, opaque = *run++;
        if (y != cur_src_y) {
            cur_src_y = y;
            cur_row_start = span_hdr;
        }
        int ys, ye;
        if (unscaled) {
            ys = dst_top + y;
            if (ys >= by_end) {
                io_run = straddle_row_start ? straddle_row_start : span_hdr;
                return;
            }
            ye = ys + 1;
        } else {
            ys = dst_top + ((y * scale_y_q8) >> 8);
            if (ys >= by_end) {
                io_run = straddle_row_start ? straddle_row_start : span_hdr;
                return;
            }
            ye = dst_top + (((y + 1) * scale_y_q8) >> 8);
        }
        if (ye <= by || ye <= ys) {
            run += count * (opaque ? 1 : 2);
            continue;
        }
        if (ye > by_end && !straddle_row_start) {
            straddle_row_start = cur_row_start;
        }
        int y0 = ys < by ? by : ys;
        int y1 = ye > by_end ? by_end : ye;

        if (scale_x_q8 == 256) {
            if (opaque) {
                int c0 = 0, c1 = 0;
                if (!flash) {
                    if (dir > 0) {
                        int dx = sx - ax + x;
                        int s0 = dx < 0 ? -dx : 0;
                        int s1 = (dx + count > FB_W) ? (FB_W - dx) : count;
                        if (s1 > s0) {
                            c0 = dx + s0;
                            c1 = dx + s1;
                            memcpy(band + (y0 - by) * FB_W + c0, run + s0, (size_t)(s1 - s0) * 2);
                        }
                    } else {
                        int d_right = sx + ax - 1 - x;
                        int i0 = (d_right >= FB_W) ? (d_right - (FB_W - 1)) : 0;
                        int i1 = (d_right - (count - 1) < 0) ? (d_right + 1) : count;
                        if (i1 > i0) {
                            uint16_t* row0 = band + (y0 - by) * FB_W;
                            for (int i = i0; i < i1; ++i) row0[d_right - i] = run[i];
                            c0 = d_right - (i1 - 1);
                            c1 = d_right - i0 + 1;
                        }
                    }
                } else {
                    c0 = (dir > 0) ? (sx - ax + x) : (sx + ax - x - count);
                    c1 = c0 + count;
                    if (c0 < 0) c0 = 0;
                    if (c1 > FB_W) c1 = FB_W;
                    if (c1 > c0) fill_span32(band + (y0 - by) * FB_W + c0, c1 - c0, flash_col);
                }
                if (c1 > c0 && y1 > y0 + 1) {
                    const uint16_t* src0 = band + (y0 - by) * FB_W + c0;
                    size_t nbytes = (size_t)(c1 - c0) * 2;
                    for (int yy = y0 + 1; yy < y1; ++yy) {
                        memcpy(band + (yy - by) * FB_W + c0, src0, nbytes);
                    }
                }
                run += count;
            } else if (y1 == y0 + 1) {
                uint16_t* row = band + (y0 - by) * FB_W;
                for (int i = 0; i < count; ++i) {
                    uint16_t c = *run++;
                    if (flash) c = flash_col;
                    int op = *run++;
                    int xx = (dir > 0) ? (sx - ax + x + i) : (sx + ax - 1 - x - i);
                    if (xx < 0 || xx >= FB_W) continue;
                    int a256 = op + (op >> 7);
                    int inv = 256 - a256;
                    uint16_t dest = row[xx];
                    int r = ((c >> 11) * a256 + (dest >> 11) * inv + 128) >> 8;
                    int g = (((c >> 5) & 63) * a256 + ((dest >> 5) & 63) * inv + 128) >> 8;
                    int b = ((c & 31) * a256 + (dest & 31) * inv + 128) >> 8;
                    row[xx] = (uint16_t)((r << 11) | (g << 5) | b);
                }
            } else {
                for (int i = 0; i < count; ++i) {
                    uint16_t c = *run++;
                    if (flash) c = flash_col;
                    int op = *run++;
                    int xx = (dir > 0) ? (sx - ax + x + i) : (sx + ax - 1 - x - i);
                    if (xx < 0 || xx >= FB_W) continue;
                    int a256 = op + (op >> 7);
                    int inv = 256 - a256;
                    int cr = (c >> 11) * a256;
                    int cg = ((c >> 5) & 63) * a256;
                    int cb = (c & 31) * a256;
                    for (int yy = y0; yy < y1; ++yy) {
                        uint16_t& dest = band[(yy - by) * FB_W + xx];
                        int r = (cr + (dest >> 11) * inv + 128) >> 8;
                        int g = (cg + ((dest >> 5) & 63) * inv + 128) >> 8;
                        int b = (cb + (dest & 31) * inv + 128) >> 8;
                        dest = (uint16_t)((r << 11) | (g << 5) | b);
                    }
                }
            }
        } else {
            if (opaque) {
                int raw_d0 = (dir > 0)
                    ? (sx + floor_div256((x - ax) * scale_x_q8))
                    : (sx + floor_div256((ax - x - count) * scale_x_q8));
                int raw_d1 = (dir > 0)
                    ? (sx + floor_div256((x + count - ax) * scale_x_q8))
                    : (sx + floor_div256((ax - x) * scale_x_q8));
                int d0 = raw_d0 < 0 ? 0 : raw_d0;
                int d1 = raw_d1 > FB_W ? FB_W : raw_d1;
                if (d1 > d0) {
                    uint16_t* row0 = band + (y0 - by) * FB_W;
                    if (flash) {
                        fill_span32(row0 + d0, d1 - d0, flash_col);
                    } else {
                        int sq = (d0 - raw_d0) * inv_sx_q16;
                        if (dir > 0) {
                            for (int xx = d0; xx < d1; ++xx) {
                                int si = sq >> 16;
                                if (si >= count) si = count - 1;
                                row0[xx] = run[si];
                                sq += inv_sx_q16;
                            }
                        } else {
                            for (int xx = d0; xx < d1; ++xx) {
                                int si = sq >> 16;
                                if (si >= count) si = count - 1;
                                row0[xx] = run[count - 1 - si];
                                sq += inv_sx_q16;
                            }
                        }
                    }
                    if (y1 > y0 + 1) {
                        const uint16_t* src0 = row0 + d0;
                        size_t nbytes = (size_t)(d1 - d0) * 2;
                        for (int yy = y0 + 1; yy < y1; ++yy) {
                            memcpy(band + (yy - by) * FB_W + d0, src0, nbytes);
                        }
                    }
                }
                run += count;
            } else {
                for (int i = 0; i < count; ++i) {
                    uint16_t c = *run++;
                    int op = *run++;
                    if (flash) c = flash_col;
                    int rel = (dir > 0) ? (x + i - ax) : (ax - 1 - x - i);
                    int d0 = sx + floor_div256(rel * scale_x_q8);
                    int d1 = sx + floor_div256((rel + 1) * scale_x_q8);
                    if (d1 <= d0) continue;
                    if (d0 < 0) d0 = 0;
                    if (d1 > FB_W) d1 = FB_W;
                    if (d1 <= d0) continue;
                    int a256 = op + (op >> 7);
                    int inv = 256 - a256;
                    int cr = (c >> 11) * a256;
                    int cg = ((c >> 5) & 63) * a256;
                    int cb = (c & 31) * a256;
                    for (int yy = y0; yy < y1; ++yy) {
                        uint16_t* row = band + (yy - by) * FB_W;
                        for (int xx = d0; xx < d1; ++xx) {
                            uint16_t dest = row[xx];
                            int r = (cr + (dest >> 11) * inv + 128) >> 8;
                            int g = (cg + ((dest >> 5) & 63) * inv + 128) >> 8;
                            int b = (cb + (dest & 31) * inv + 128) >> 8;
                            row[xx] = (uint16_t)((r << 11) | (g << 5) | b);
                        }
                    }
                }
            }
        }
    }
    io_run = straddle_row_start ? straddle_row_start : end;
}

static inline void blit_span_band(uint16_t* band, int by, int bh,
                                  const uint16_t* run, const uint16_t* end,
                                  int w, int h, int ax, int ay,
                                  int sx, int sy, int8_t dir, uint8_t flash,
                                  int scale_x_q8 = 256, int scale_y_q8 = 256)
{
    const uint16_t* cur = run;
    blit_span_band_cursor(band, by, bh, cur, end, w, h, ax, ay, sx, sy, dir, flash, scale_x_q8, scale_y_q8);
}

static int sprite_frame(const Actor& a)
{
    uint8_t ba = a.ally ? a.base_atlas : a.base_atlas;
    int windup = a.windup_f ? (int)a.windup_f : (a.ally ? 6 : (ba == 3 ? 16 : (ba == 2 ? 20 : 14)));
    int recover = a.recover_f ? (int)a.recover_f : (a.ally ? 7 : (ba == 3 ? 16 : 18));

    if (a.state == A_IDLE) return (motion_tick / 7) % 4;
    if (a.state == A_WALK) {
        int div = (fabsf(a.vx) + fabsf(a.vy) >= 2.4f) ? 2 : 3;
        return 4 + (motion_tick / div) % 8;
    }
    if (a.state == A_VICTORY) {
        if (a.ally && ba == 0) return ((motion_tick / 6) % 2) ? 27 : 26;
        if (a.ally && ba == 1) return ((motion_tick / 7) % 2) ? 31 : 30;
        if (a.ally && ba == 2) return ((motion_tick / 8) % 2) ? 16 : 15;
        return 0;
    }

    if (a.ally && ba == 0) {
        uint8_t c = a.active_combo % 3;
        switch (a.state) {
        case A_WINDUP: {
            int elapsed = windup - (int)a.timer;
            if (c == 0) return (elapsed <= 1) ? 12 : (elapsed <= 2 ? 13 : 14);
            if (c == 1) return (elapsed <= 1) ? 18 : (elapsed <= 2 ? 19 : 20);
            return (elapsed <= 1) ? 25 : (elapsed <= 2 ? 26 : 27);
        }
        case A_STRIKE:
            return (c == 0) ? 15 : (c == 1 ? 21 : 28);
        case A_RECOVER: {
            int p = (recover - (int)a.timer) * 2 / (recover > 0 ? recover : 1);
            if (p > 1) p = 1;
            if (c == 0) return 16 + p;
            if (c == 1) return 22 + p;
            return 29;
        }
        case A_HURT: return (a.timer > 6) ? 30 : ((a.timer > 3) ? 31 : 32);
        case A_DOWN:
            if (a.timer < 18) return 34;
            return (a.timer > 136) ? 33 : ((a.timer > 124) ? 34 : 35);
        case A_SPECIAL:
            if (a.timer > 30) return 26;
            if (a.timer > 16) return 27;
            if (a.timer > 6)  return 28;
            return 29;
        default: return 0;
        }
    }

    if (a.ally && ba == 1) {
        switch (a.state) {
        case A_WINDUP: {
            int p = (windup - (int)a.timer) * 3 / (windup > 0 ? windup : 1);
            return 12 + (p > 2 ? 2 : (p < 0 ? 0 : p));
        }
        case A_STRIKE: return (a.timer > 2) ? 15 : 16;
        case A_RECOVER: {
            int p = (recover - (int)a.timer) * 3 / (recover > 0 ? recover : 1);
            return 17 + (p > 2 ? 2 : (p < 0 ? 0 : p));
        }
        case A_HURT: return (a.timer > 6) ? 20 : ((a.timer > 3) ? 21 : 22);
        case A_DOWN:
            if (a.timer < 22) return (a.timer < 10) ? 29 : 28;
            return (a.timer > 136) ? 25 : ((a.timer > 124) ? 26 : 27);
        case A_SPECIAL:
            if (a.timer > 34) return 30;
            if (a.timer > 24) return 31;
            if (a.timer > 14) return 32;
            if (a.timer > 6)  return 33;
            return (a.timer > 2) ? 34 : 35;
        default: return 0;
        }
    }

    if (a.ally && ba == 2) {
        switch (a.state) {
        case A_WINDUP: {
            int elapsed = windup - (int)a.timer;
            if (elapsed <= 2) return 12;
            if (elapsed <= 5) return 14;
            return 16;
        }
        case A_STRIKE: return (a.timer > 3) ? 18 : 19;
        case A_RECOVER: return (a.timer > recover / 2) ? 20 : 21;
        case A_HURT: return (a.timer > 4) ? 24 : 25;
        case A_DOWN:
            if (a.timer < 22) return (a.timer < 10) ? 29 : 28;
            return (a.timer > 136) ? 25 : ((a.timer > 124) ? 26 : 27);
        case A_SPECIAL:
            if (a.timer > 34) return 30;
            if (a.timer > 29) return 31;
            if (a.timer > 24) return 32;
            if (a.timer > 20) return 33;
            if (a.timer > 8)  return 34;
            return 35;
        default: return 0;
        }
    }

    /* Enemies & Bosses by physical chassis base_atlas (0=slime, 1=bat, 2=golem, 3=boss) */
    switch (ba) {
    case 0: /* slime */
        switch (a.state) {
        case A_WINDUP: return (a.timer > 8) ? 12 : ((a.timer > 3) ? 13 : 14);
        case A_STRIKE: return (a.timer > 2) ? 15 : 16;
        case A_RECOVER: return 17;
        case A_HURT: return (a.timer > 7) ? 20 : ((a.timer > 4) ? 21 : ((a.timer > 2) ? 22 : 23));
        case A_DOWN: return (a.timer > 50) ? 24 : ((a.timer > 42) ? 25 : ((a.timer > 20) ? 26 : 27));
        case A_SPECIAL: return (a.timer > 10) ? 30 : ((a.timer > 4) ? 32 : 33);
        default: return 0;
        }
    case 1: /* bat */
        switch (a.state) {
        case A_WINDUP: return (a.timer > 9) ? 12 : ((a.timer > 4) ? 13 : 14);
        case A_STRIKE: return (a.timer > 3) ? 16 : 17;
        case A_RECOVER: return (a.timer > 8) ? 18 : 19;
        case A_HURT: return (a.timer > 5) ? 22 : 23;
        case A_DOWN: return (a.timer > 50) ? 24 : ((a.timer > 42) ? 25 : ((a.timer > 22) ? 26 : 27));
        case A_SPECIAL: return (a.timer > 6) ? 31 : 32;
        default: return 0;
        }
    case 2: /* golem (regular & Colossus/Sovereign bosses) */
        switch (a.state) {
        case A_WINDUP: return (a.timer > 14) ? 12 : ((a.timer > 7) ? 14 : 16);
        case A_STRIKE: return (a.timer > 4) ? 17 : ((a.timer > 2) ? 18 : 19);
        case A_RECOVER: return (a.timer > 10) ? 20 : ((a.timer > 5) ? 21 : 22);
        case A_HURT: return (a.timer > 5) ? 25 : 26;
        case A_DOWN: return (a.timer > 50) ? 25 : ((a.timer > 40) ? 26 : 27);
        case A_SPECIAL:
            if (a.timer > 34) return 30;
            if (a.timer > 15) return 31;
            if (a.timer > 11) return 32;
            if (a.timer > 4)  return 33;
            return 34;
        default: return 0;
        }
    case 3: /* boss */
    default:
        switch (a.state) {
        case A_WINDUP: return (a.timer > 10) ? 12 : ((a.timer > 5) ? 13 : 14);
        case A_STRIKE: return (a.timer > 4) ? 15 : ((a.timer > 2) ? 16 : 17);
        case A_RECOVER: return (a.timer > 10) ? 18 : ((a.timer > 5) ? 19 : 20);
        case A_HURT: return (a.timer > 4) ? 21 : 22;
        case A_DOWN: return (a.timer > 86) ? 24 : ((a.timer > 70) ? 25 : ((a.timer > 45) ? 26 : 27));
        case A_SPECIAL:
            if (a.timer > 34) return 30;
            if (a.timer > 15) return 31;
            if (a.timer > 11) return 32;
            if (a.timer > 4)  return 33;
            return 34;
        default: return 0;
        }
    }
}

/* Preview pose lookup with per-slot temporary palette remap (used in Draft Cards & Codex) */
static const uint16_t* ensure_preview_pose(uint8_t base_atlas, int pose, bool use_orig_pal,
                                           uint16_t pal_pri, uint16_t pal_sec, int cache_slot,
                                           uint32_t& out_words, int& out_size)
{
    uint8_t ba = base_atlas % 3;
    const SpriteAtlas& atlas = ally_atlases[ba];
    out_size = atlas.size;
    pose %= 36;
    const uint16_t* flash_run = atlas.spans + atlas.offsets[pose];
    uint32_t words = atlas.offsets[pose + 1] - atlas.offsets[pose];
    out_words = words;
    uint32_t pkey = use_orig_pal ? 0xFFFFFFFFu : (((uint32_t)pal_pri << 16) | pal_sec);
    if (cache_slot >= 0 && cache_slot < ACTOR_SLOTS && actor_pose_buf[cache_slot]) {
        if (cached_atlas_id[cache_slot] != (int8_t)ba || cached_pose[cache_slot] != (int16_t)pose || cached_pal_key[cache_slot] != pkey) {
            memcpy(actor_pose_buf[cache_slot], flash_run, words * 2);
            if (!use_orig_pal) {
                remap_span_palette(actor_pose_buf[cache_slot], words, true, ba, pal_pri, pal_sec);
            }
            cached_atlas_id[cache_slot] = (int8_t)ba;
            cached_pose[cache_slot] = (int16_t)pose;
            cached_pal_key[cache_slot] = pkey;
        }
        return actor_pose_buf[cache_slot];
    }
    return flash_run;
}

static const uint16_t* ensure_actor_pose(const Actor& a, int slot_idx, uint32_t& out_words, int& out_size, int& out_pose)
{
    uint8_t ba = a.ally ? (a.base_atlas % 3) : (a.base_atlas % 4);
    int atlas_id = a.ally ? (int)ba : (3 + (int)ba);
    const SpriteAtlas& atlas = a.ally ? ally_atlases[ba] : enemy_atlases[ba];
    out_size = atlas.size;
    int pose = sprite_frame(a);
    out_pose = pose;
    const uint16_t* flash_run = atlas.spans + atlas.offsets[pose];
    uint32_t words = atlas.offsets[pose + 1] - atlas.offsets[pose];
    out_words = words;
    uint32_t pkey = a.use_orig_pal ? 0xFFFFFFFFu : (((uint32_t)a.pal_pri << 16) | a.pal_sec);
    if (slot_idx >= 0 && slot_idx < ACTOR_SLOTS && actor_pose_buf[slot_idx]) {
        if (cached_atlas_id[slot_idx] != (int8_t)atlas_id ||
            cached_pose[slot_idx] != (int16_t)pose ||
            cached_pal_key[slot_idx] != pkey) {
            memcpy(actor_pose_buf[slot_idx], flash_run, words * 2);
            if (!a.use_orig_pal) {
                remap_span_palette(actor_pose_buf[slot_idx], words, a.ally, ba, a.pal_pri, a.pal_sec);
            }
            cached_atlas_id[slot_idx] = (int8_t)atlas_id;
            cached_pose[slot_idx] = (int16_t)pose;
            cached_pal_key[slot_idx] = pkey;
        }
        return actor_pose_buf[slot_idx];
    }
    return flash_run;
}

/* Compute active squash & stretch Q8 scale (256 = 1.0x) (#120).
   Keeps sx_q8 == 256 for windup/strike and tail decay so blit_span_band stays on the memcpy fast path. */
static void actor_squash(const Actor& a, int& sx_q8, int& sy_q8)
{
    sx_q8 = 256; sy_q8 = 256;
    if (a.squash_t == 0) {
        if (a.state == A_WINDUP) sy_q8 = 242;
        else if (a.state == A_STRIKE) sy_q8 = 266;
        return;
    }
    int amp = a.squash_t * 5;
    if (amp > 42) amp = 42;
    bool horiz = (a.squash_t >= 4);
    switch (a.squash_kind) {
    case 1: sx_q8 = horiz ? (256 - (amp * 3) / 4) : 256; sy_q8 = 256 + amp; break;
    case 2: sx_q8 = horiz ? (256 + amp) : 256; sy_q8 = 256 - (amp * 3) / 4; break;
    case 3: sx_q8 = horiz ? (256 - amp / 2) : 256; sy_q8 = 256 + (amp * 3) / 4; break;
    default: break;
    }
}
