/**
 * sandbox.cpp — 落下砂モデル物理エンジン実装
 */
#include "sandbox.h"
#include <string.h>
#include <esp_heap_caps.h>

namespace sandbox {

/* ------------------------------------------------------------------ */
/* 内部状態                                                            */
/* ------------------------------------------------------------------ */

static uint8_t* grid = nullptr;   /* [GW*GH] セルの素材 */
static uint8_t* moved = nullptr;  /* [GW*GH] 今フレームで移動済みフラグ */
static uint32_t tick = 0;
static uint32_t rng = 12345u;

static inline int IX(int x, int y) { return y * GW + x; }

static inline uint8_t at(int x, int y)
{
    if (x < 0 || x >= GW || y < 0 || y >= GH) return ROCK; /* 外周は壁 */
    return grid[IX(x, y)];
}

static inline uint32_t rnd()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static inline uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + tick * 0u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* ------------------------------------------------------------------ */
/* 移動ヘルパ                                                          */
/* ------------------------------------------------------------------ */

static inline void moveCell(int x, int y, int nx, int ny)
{
    uint8_t m = grid[IX(x, y)];
    uint8_t d = grid[IX(nx, ny)];
    grid[IX(nx, ny)] = m;
    grid[IX(x, y)] = d;
    moved[IX(nx, ny)] = 1;
    moved[IX(x, y)] = 1;
}

/* ------------------------------------------------------------------ */
/* 成長ルール (水があると結晶が育つ)                                     */
/* ------------------------------------------------------------------ */

static const int NDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int NDY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

/* 上向き成分のある隣接マスを優先して成長先を選ぶ。
 * 空きマスが無い場合 (完全に水に沈んでいる場合) は水を直接結晶化する。 */
static bool growCell(int x, int y, uint32_t prob_per1000)
{
    if ((int)(rnd() % 1000u) >= (int)prob_per1000) return false;

    bool has_water = false;
    int up[8], up_n = 0, all[8], all_n = 0;
    int water_at = -1, water_n = 0;

    for (int i = 0; i < 8; i++) {
        int nx = x + NDX[i], ny = y + NDY[i];
        uint8_t m = at(nx, ny);
        if (m == WATER) { has_water = true; water_n++; if ((int)(rnd() % water_n) == 0) water_at = i; }
        else if (m == EMPTY) {
            all[all_n++] = i;
            if (NDY[i] <= 0) up[up_n++] = i;   /* 上方向・水平を優先 */
        }
    }
    if (!has_water) return false;

    if (all_n > 0) {
        /* 空きマスへ成長し、隣接水を 1 つ消費 */
        int pick = (up_n > 0 && (rnd() % 100u) < 70u) ? up[rnd() % up_n] : all[rnd() % all_n];
        grid[IX(x + NDX[pick], y + NDY[pick])] = CRYSTAL;
        moved[IX(x + NDX[pick], y + NDY[pick])] = 1;
        if (water_at >= 0) grid[IX(x + NDX[water_at], y + NDY[water_at])] = EMPTY;
    } else {
        /* 空きが無い: 隣接水そのものを結晶化する */
        if (water_at >= 0) grid[IX(x + NDX[water_at], y + NDY[water_at])] = CRYSTAL;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* 各素材の挙動                                                        */
/* ------------------------------------------------------------------ */

static void sandStep(int x, int y)
{
    uint8_t below = at(x, y + 1);
    if (below == EMPTY) { moveCell(x, y, x, y + 1); return; }
    if (below == WATER) { moveCell(x, y, x, y + 1); return; } /* 水中に沈む (swap) */

    int d = (rnd() & 1) ? 1 : -1;
    for (int k = 0; k < 2; k++) {
        int nx = x + (k == 0 ? d : -d);
        uint8_t diag = at(nx, y + 1);
        if (diag == EMPTY || diag == WATER) { moveCell(x, y, nx, y + 1); return; }
    }
}

static void waterStep(int x, int y)
{
    if (at(x, y + 1) == EMPTY) { moveCell(x, y, x, y + 1); return; }

    int d = (rnd() & 1) ? 1 : -1;
    if (at(x + d, y + 1) == EMPTY) { moveCell(x, y, x + d, y + 1); return; }
    if (at(x - d, y + 1) == EMPTY) { moveCell(x, y, x - d, y + 1); return; }

    /* 水平に流れる (左右どちらか空いている方) */
    if (at(x + d, y) == EMPTY) { moveCell(x, y, x + d, y); return; }
    if (at(x - d, y) == EMPTY) { moveCell(x, y, x - d, y); return; }
}

/* ------------------------------------------------------------------ */
/* デモ用エミッタ (Phase 4 で施設に置き換え)                             */
/* ------------------------------------------------------------------ */

static void ambientEmitters()
{
    /* 左: 水晶の泉 (水滴) */
    if (tick % 6 == 0) {
        int x = 62 + (int)(rnd() % 12);
        if (grid[IX(x, 1)] == EMPTY) grid[IX(x, 1)] = WATER;
    }
    /* 右: 鉱脈の巣 (砂) */
    if (tick % 5 == 0) {
        int x = 202 + (int)(rnd() % 24);
        if (grid[IX(x, 1)] == EMPTY) grid[IX(x, 1)] = SAND;
    }
    /* たまに結晶の種が落ちてくる */
    if (tick % 1200 == 0) {
        int x = 20 + (int)(rnd() % (GW - 40));
        if (grid[IX(x, 1)] == EMPTY) grid[IX(x, 1)] = SEED;
    }
}

/* ------------------------------------------------------------------ */
/* 公開 API                                                            */
/* ------------------------------------------------------------------ */

void init()
{
    if (!grid) {
        grid = (uint8_t*)heap_caps_calloc(GW * GH, 1, MALLOC_CAP_SPIRAM);
        moved = (uint8_t*)heap_caps_calloc(GW * GH, 1, MALLOC_CAP_SPIRAM);
    }
    memset(grid, EMPTY, GW * GH);
    memset(moved, 0, GW * GH);
    tick = 0;

    /* --- 床 --- */
    for (int x = 0; x < GW; x++)
        for (int y = GH - 3; y < GH; y++) grid[IX(x, y)] = ROCK;

    /* --- 左: 滝壺 (岩の鉢 + 水溜まり + 底に種) --- */
    for (int x = 30; x <= 120; x++)
        for (int y = GH - 28; y <= GH - 26; y++) grid[IX(x, y)] = ROCK;
    for (int y = GH - 36; y <= GH - 29; y++) { grid[IX(30, y)] = ROCK; grid[IX(120, y)] = ROCK; }
    for (int x = 33; x <= 117; x++)
        for (int y = GH - 35; y <= GH - 29; y++) grid[IX(x, y)] = WATER;
    for (int x = 50; x <= 90; x += 10) grid[IX(x, GH - 29)] = SEED;

    /* --- 右: 砂の溜まり (壁で受ける) --- */
    for (int y = GH - 58; y <= GH - 4; y++) grid[IX(188, y)] = ROCK;
    for (int x = 192; x <= 225; x++)
        for (int y = GH - 20; y <= GH - 4; y++) grid[IX(x, y)] = SAND;

    /* --- 中央: 岩の段差と種 --- */
    for (int x = 130; x <= 180; x++)
        for (int y = GH - 12; y <= GH - 10; y++) grid[IX(x, y)] = ROCK;
    grid[IX(150, GH - 13)] = SEED;
    grid[IX(160, GH - 13)] = SEED;
    grid[IX(145, GH - 4)] = SEED;

    rng = (uint32_t)(tick ^ 0x9E3779B9u) | 1u;
}

void step()
{
    if (!grid) return;
    tick++;
    ambientEmitters();

    memset(moved, 0, GW * GH);
    int dir = (tick & 1) ? -1 : 1;   /* 水平走査方向を毎フレーム反転 */

    for (int y = GH - 1; y >= 0; y--) {
        for (int i = 0; i < GW; i++) {
            int x = (dir > 0) ? i : (GW - 1 - i);
            int idx = IX(x, y);
            uint8_t m = grid[idx];
            if (m == EMPTY || moved[idx]) continue;
            switch (m) {
            case SAND:    sandStep(x, y); break;
            case WATER:   waterStep(x, y); break;
            case SEED:    growCell(x, y, 25); break;    /* 種: 成長しやすい */
            case CRYSTAL: growCell(x, y, 6);  break;    /* 結晶: ゆるやかに伝播 */
            default: break;
            }
        }
    }
}

void stamp(int gx, int gy, int radius, Mat m)
{
    if (!grid) return;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int x = gx + dx, y = gy + dy;
            if (x < 0 || x >= GW || y < 0 || y >= GH) continue;
            int idx = IX(x, y);
            if (m == EMPTY) { grid[idx] = EMPTY; }
            else if (grid[idx] == EMPTY) { grid[idx] = (uint8_t)m; }
        }
    }
}

int count(Mat m)
{
    if (!grid) return 0;
    int n = 0;
    for (int i = 0; i < GW * GH; i++) if (grid[i] == (uint8_t)m) n++;
    return n;
}

void render(uint16_t* fb)
{
    if (!grid || !fb) return;

    /* RGB565 カラーパレット */
    const uint16_t C_EMPTY = ((0x12 & 0xF8) << 8) | ((0x12 & 0xFC) << 3) | (0x2A >> 3);
    const uint16_t C_ROCK[3] = {
        ((0x5E & 0xF8) << 8) | ((0x5E & 0xFC) << 3) | (0x70 >> 3),
        ((0x6C & 0xF8) << 8) | ((0x6C & 0xFC) << 3) | (0x82 >> 3),
        ((0x52 & 0xF8) << 8) | ((0x52 & 0xFC) << 3) | (0x64 >> 3),
    };
    const uint16_t C_SAND[3] = {
        ((0xD9 & 0xF8) << 8) | ((0xB3 & 0xFC) << 3) | (0x6C >> 3),
        ((0xC7 & 0xF8) << 8) | ((0x9F & 0xFC) << 3) | (0x55 >> 3),
        ((0xE3 & 0xF8) << 8) | ((0xC2 & 0xFC) << 3) | (0x7E >> 3),
    };
    const uint16_t C_WATER[4] = {
        ((0x2A & 0xF8) << 8) | ((0x6B & 0xFC) << 3) | (0xE0 >> 3),
        ((0x3F & 0xF8) << 8) | ((0x8B & 0xFC) << 3) | (0xF5 >> 3),
        ((0x28 & 0xF8) << 8) | ((0x58 & 0xFC) << 3) | (0xC8 >> 3),
        ((0x55 & 0xF8) << 8) | ((0x9E & 0xFC) << 3) | (0xFF >> 3),
    };
    const uint16_t C_SEED = ((0x7F & 0xF8) << 8) | ((0xF4 & 0xFC) << 3) | (0xFF >> 3);
    const uint16_t C_CRYS[3] = {
        ((0x9F & 0xF8) << 8) | ((0xF8 & 0xFC) << 3) | (0xE8 >> 3),
        ((0x6E & 0xF8) << 8) | ((0xD5 & 0xFC) << 3) | (0xC8 >> 3),
        ((0xD8 & 0xF8) << 8) | ((0xFF & 0xFC) << 3) | (0xFF >> 3),
    };

    const int stride = FB_W;

    for (int gy = 0; gy < GH; gy++) {
        uint16_t* rowA = fb + (gy * CELL) * stride;
        uint16_t* rowB = rowA + stride;
        for (int gx = 0; gx < GW; gx++) {
            uint8_t m = grid[IX(gx, gy)];
            uint16_t c;
            uint32_t h = 0;
            switch (m) {
            case EMPTY: c = C_EMPTY; break;
            case ROCK:  c = C_ROCK[hash2(gx, gy) % 3]; break;
            case SAND:  c = C_SAND[hash2(gx, gy) % 3]; break;
            case WATER:
                h = (hash2(gx, gy) + (tick >> 3)) & 3;
                c = C_WATER[h];
                break;
            case SEED:  c = C_SEED; break;
            case CRYSTAL:
                /* ときどき白くきらめく */
                c = (((hash2(gx, gy) ^ (tick >> 2)) % 100u) < 8u) ? C_CRYS[2]
                    : C_CRYS[hash2(gx, gy) % 2];
                break;
            default:    c = C_EMPTY; break;
            }
            uint32_t pair = ((uint32_t)c << 16) | c;  /* 2x2 ブロックは u32 2 回で書ける */
            int ox = gx * CELL;
            uint32_t* a = (uint32_t*)(rowA + ox);
            uint32_t* b = (uint32_t*)(rowB + ox);
            a[0] = pair;
            b[0] = pair;
        }
    }
}

} // namespace sandbox
