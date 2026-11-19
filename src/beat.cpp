/**
 * beat.cpp — 『ルミの水晶庭園』v4.0 ローグライト オートバトルエンジン
 *
 * - 全50ウェーブ有限ラン (初期1人弱小スタート → Lootbox 3択ドラフトで構築 → Wave50でエンディング)
 * - 64キャラクター / 36チャーム / 6ボスバリエーション (50% / 30% / 10% しきい値固有アクション)
 * - 嫌がらせ敵AI (酸床/火傷クレーター/呪い針スロウ/背面ダイブ/反射棘バリア)
 * - 非停止インパクト表現 (全体ヒットストップなし)・layer=2縁取り数値・毎フレームミニマップ更新
 * - 32行内部SRAMバンド合成器 + RGB565 キャッシュパレットリマップ
 */
#include "beat.h"
#include "game_params.h"
#include "garden_game.h"
#include "roguelite_data.h"
#include "generated/lumi_atlas.h"
#include "generated/mira_atlas.h"
#include "generated/goro_atlas.h"
#include "generated/slime_atlas.h"
#include "generated/bat_atlas.h"
#include "generated/golem_atlas.h"
#include "generated/boss_atlas.h"
#include "generated/fx_combat_atlas.h"
#include "generated/scenery_assets.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace beat {

/* ---------------- カラー定数 ---------------- */
static inline uint16_t RGB(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static const uint16_t C_WHITE   = RGB(255, 255, 255);
static const uint16_t C_BLACK   = RGB(0x0A, 0x0C, 0x16);
static const uint16_t C_AQUA    = RGB(0x70, 0xE2, 0xF4);
static const uint16_t C_RED     = RGB(0xE8, 0x42, 0x54);
static const uint16_t C_GOLD    = RGB(0xFF, 0xCC, 0x44);
static const uint16_t C_VIOLET  = RGB(0xB8, 0x78, 0xFF);
static const uint16_t C_MINT    = RGB(0x64, 0xF0, 0x90);
static const uint16_t C_ORANGE  = RGB(0xF8, 0x88, 0x30);
static const uint16_t C_LIME    = RGB(0x78, 0xEA, 0x48);
static const uint16_t C_SHADOW0 = RGB(0x0E, 0x1A, 0x1A);
static const uint16_t C_SHADOW1 = RGB(0x16, 0x26, 0x24);

static inline uint16_t rarity_color(uint8_t r)
{
    switch (r) {
    case game::RAR_N:   return RGB(0xA8, 0xD0, 0xE8);
    case game::RAR_R:   return RGB(0x60, 0xB8, 0xFF);
    case game::RAR_SR:  return C_VIOLET;
    case game::RAR_SSR: return C_GOLD;
    default:            return C_WHITE;
    }
}
static inline uint16_t rarity_bg(uint8_t r)
{
    switch (r) {
    case game::RAR_N:   return RGB(0x1C, 0x32, 0x48);
    case game::RAR_R:   return RGB(0x18, 0x3A, 0x6C);
    case game::RAR_SR:  return RGB(0x46, 0x1E, 0x6E);
    case game::RAR_SSR: return RGB(0x6A, 0x46, 0x0C);
    default:            return RGB(0x1C, 0x32, 0x48);
    }
}

/* ---------------- 32行 内部SRAM バンド合成バッファ ---------------- */
static constexpr int BAND_H = 32;
static constexpr int NUM_BANDS = (FB_H + BAND_H - 1) / BAND_H; /* 11 */
static uint16_t* band_buf = nullptr;
static uint16_t* sky_buf  = nullptr;
static uint16_t* gnd_buf  = nullptr;
static uint16_t* prop_spans_ram    = nullptr;
static uint16_t* scenery_spans_ram = nullptr;
static uint16_t* vfx_spans_ram     = nullptr;

struct TightBounds { uint8_t x0, y0, x1, y1; };
static TightBounds actor_bounds[7][36];
static TightBounds vfx_bounds[vfx_sprites::FXS_COUNT];
static TightBounds scenery_bounds[scenery::SC_COUNT];

static int      prev_icam[2] = {-99999, -99999};
static int      prev_ox[2]   = {0, 0};
static int      prev_oy[2]   = {0, 0};
static uint8_t  prev_tint[2] = {255, 255};
static uint16_t prev_tmask[2][NUM_BANDS];

struct HudState {
    int lead_hw;
    char crys[28];
    char goal[28];
    char meta_r[24];
    uint32_t wave;
    int pet_s;
    int hp_int[ALLY_N];
    int hpmax_int[ALLY_N];
    int mp_int[ALLY_N];
    int sh_int[ALLY_N];
    uint8_t active_mask;
    uint8_t down_mask;
    uint8_t status_mask[ALLY_N];
    uint8_t tid[ALLY_N];
    uint8_t rank[ALLY_N];
    bool box_ready;
    bool draft_open;
    uint8_t free_boxes;
    uint8_t hud_mode;   /* 0=battle, 1=title, 2=opening */
    bool title_blink;
    uint8_t op_scene;
    uint8_t op_bar;
    bool valid;
};
static HudState hud_st[2] = {};

static void invalidate_all_bands()
{
    prev_icam[0] = prev_icam[1] = -99999;
    prev_tint[0] = prev_tint[1] = 255;
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < NUM_BANDS; ++i) prev_tmask[b][i] = 0x7FFFu;
    }
}

static int64_t prof_restore_us = 0, prof_draw_us = 0;
static uint32_t prof_n = 0;

/* ---------------- 低レベルバンドプリミティブ ---------------- */
static inline void fill_span32(uint16_t* dst, int count, uint16_t c)
{
    if (count <= 0) return;
    if ((uintptr_t)dst & 2) { *dst++ = c; count--; }
    uint32_t c2 = ((uint32_t)c << 16) | c;
    uint32_t* d32 = (uint32_t*)dst;
    int pairs = count >> 1;
    while (pairs-- > 0) *d32++ = c2;
    if (count & 1) *(uint16_t*)d32 = c;
}

static void fill_rect_band(uint16_t* band, int by, int bh, int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < 0 ? 0 : x;
    int x1 = (x + w > FB_W) ? FB_W : (x + w);
    if (x1 <= x0) return;
    int y0 = y < by ? by : y;
    int y1 = (y + h > by + bh) ? (by + bh) : (y + h);
    int len = x1 - x0;
    for (int yy = y0; yy < y1; ++yy) {
        fill_span32(band + (yy - by) * FB_W + x0, len, c);
    }
}

static void draw_rect_frame_band(uint16_t* band, int by, int bh, int x, int y, int w, int h, int t, uint16_t c)
{
    fill_rect_band(band, by, bh, x, y, w, t, c);
    fill_rect_band(band, by, bh, x, y + h - t, w, t, c);
    fill_rect_band(band, by, bh, x, y, t, h, c);
    fill_rect_band(band, by, bh, x + w - t, y, t, h, c);
}

static void draw_ellipse_ring_band(uint16_t* band, int by, int bh,
                                   int cx, int cy, int rx, int ry, uint16_t c_edge, uint16_t c_fill, bool fill_pulse)
{
    if (rx <= 3 || ry <= 2) return;
    int y0 = (cy - ry < by) ? by : (cy - ry);
    int y1 = (cy + ry + 1 > by + bh) ? (by + bh) : (cy + ry + 1);
    float inv_ry2 = 1.0f / (float)(ry * ry);
    for (int yy = y0; yy < y1; ++yy) {
        int dy = yy - cy;
        float t = 1.0f - (dy * dy) * inv_ry2;
        if (t <= 0.0f) continue;
        int span = (int)(sqrtf(t) * rx);
        int x0 = cx - span;
        int x1 = cx + span;
        int inner_span = (int)(sqrtf(t) * (rx - 3));
        if (abs(dy) >= ry - 2) inner_span = 0;
        uint16_t* row = band + (yy - by) * FB_W;
        int cx0 = x0 < 0 ? 0 : x0;
        int cx1 = x1 >= FB_W ? FB_W - 1 : x1;
        if (fill_pulse && inner_span > 0) {
            int ix0 = cx - inner_span; if (ix0 < 0) ix0 = 0;
            int ix1 = cx + inner_span; if (ix1 >= FB_W) ix1 = FB_W - 1;
            for (int xx = ix0; xx <= ix1; xx += 2) {
                uint16_t d = row[xx];
                row[xx] = (uint16_t)(((d >> 1) & 0x7BEF) + ((c_fill >> 1) & 0x7BEF));
            }
        }
        int lx1 = cx - inner_span; if (lx1 > FB_W) lx1 = FB_W;
        for (int xx = cx0; xx < lx1; ++xx) row[xx] = c_edge;
        int rx0 = cx + inner_span + 1; if (rx0 < 0) rx0 = 0;
        for (int xx = rx0; xx <= cx1; ++xx) row[xx] = c_edge;
    }
}

/* ---------------- 5x7 標準ビットマップフォント (docs/UX_GUIDELINE.md 準拠) ---------------- */
#include "font5x7.inc"

static inline const uint8_t* glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 0x20 || c > 0x5F) return FONT5X7[0];
    return FONT5X7[(uint8_t)c - 0x20];
}
static int draw_char_band(uint16_t* band, int by, int bh, int x, int y, char c, int scale, uint16_t col)
{
    if (scale < 2) scale = 2;
    if (x + 5 * scale <= 0 || x >= FB_W) return 6 * scale;
    if (y + 7 * scale <= by || y >= by + bh) return 6 * scale;
    const uint8_t* g = glyph(c);
    for (int row = 0; row < 7; row++) {
        uint8_t bits = g[row];
        if (!bits) continue;
        int ry = y + row * scale;
        if (ry + scale <= by || ry >= by + bh) continue;
        for (int col2 = 0; col2 < 5; col2++)
            if (bits & (1 << (4 - col2)))
                fill_rect_band(band, by, bh, x + col2 * scale, ry, scale, scale, col);
    }
    return 6 * scale;
}
static int draw_text_band(uint16_t* band, int by, int bh, int x, int y, const char* s, int scale, uint16_t col)
{
    if (scale < 2) scale = 2;
    if (!s || (y + 7 * scale <= by || y >= by + bh)) return x + (s ? (int)strlen(s) : 0) * 6 * scale;
    for (const char* p = s; *p; p++) x += draw_char_band(band, by, bh, x, y, *p, scale, col);
    return x;
}
static int draw_text_shadow_band(uint16_t* band, int by, int bh, int x, int y, const char* s, int scale, uint16_t col, uint16_t sh = C_BLACK)
{
    if (scale < 2) scale = 2;
    int d = (scale >= 3) ? 2 : 1;
    draw_text_band(band, by, bh, x + d, y + d, s, scale, sh);
    return draw_text_band(band, by, bh, x, y, s, scale, col);
}
static int draw_text_outline_band(uint16_t* band, int by, int bh, int x, int y, const char* s, int scale, uint16_t col)
{
    if (scale < 2) scale = 2;
    int d = (scale >= 3) ? 2 : 1;
    draw_text_band(band, by, bh, x - 1, y,     s, scale, C_BLACK);
    draw_text_band(band, by, bh, x + 1, y,     s, scale, C_BLACK);
    draw_text_band(band, by, bh, x,     y - 1, s, scale, C_BLACK);
    draw_text_band(band, by, bh, x,     y + 1, s, scale, C_BLACK);
    draw_text_band(band, by, bh, x + d, y + d, s, scale, C_BLACK);
    return draw_text_band(band, by, bh, x, y, s, scale, col);
}
static void draw_number_shadow_band(uint16_t* band, int by, int bh, int x, int y, int value, int scale, uint16_t col)
{
    char tmp[14];
    snprintf(tmp, sizeof(tmp), "%d", value);
    draw_text_outline_band(band, by, bh, x, y, tmp, scale, col);
}
static int text_w(const char* s, int scale)
{
    if (scale < 2) scale = 2;
    return s ? (int)strlen(s) * 6 * scale : 0;
}

/* ---------------- アクタ & シーン状態 ---------------- */
enum AState : uint8_t { A_IDLE, A_WALK, A_WINDUP, A_STRIKE, A_RECOVER, A_HURT, A_DOWN, A_SPECIAL, A_VICTORY };
enum EType  : uint8_t { E_SLIME, E_BAT, E_GOLEM, E_BOSS };
enum DirectorMode : uint8_t { DIR_TITLE, DIR_OPENING, DIR_BATTLE, DIR_BOSS_INTRO, DIR_WAVE_CLEAR, DIR_BOSS_VICTORY, DIR_GAME_OVER, DIR_ENDING };

struct Actor {
    bool alive = false;
    bool ally = false;
    uint8_t type = 0;            /* enemy: E_SLIME..E_BOSS / ally: slot style or unused */
    uint8_t base_atlas = 0;      /* ally: 0..2 / enemy: 0..3 (bossもGolem骨格=2可) */
    uint8_t talent_id = 0;       /* ally: 0..63 / enemy: boss_id(0..5) */
    uint8_t sub_kind = 0;        /* enemy: 0=acid/sniper/mortar, 1=leech/diver/thorn, 2=bomber */
    uint8_t rank = 1;            /* ally: 1..9 */
    bool use_orig_pal = true;
    uint16_t pal_pri = 0;
    uint16_t pal_sec = 0;
    uint8_t windup_f = 6;
    uint8_t recover_f = 7;

    float x = 0, y = 0, z = 0;
    int8_t dir = 1;
    uint8_t state = A_IDLE;
    float hp = 10, hpmax = 10;
    float shield = 0;
    float vx = 0, vy = 0, vz = 0;
    uint16_t timer = 0;
    uint8_t combo = 0;
    uint8_t active_combo = 0;
    uint8_t air_combo = 0;
    uint8_t flash = 0;
    bool invuln = false;
    uint16_t invuln_t = 0;
    float rage = 0;
    float home_x = 0;
    float wander_t = 0;
    uint8_t spawn_t = 0;
    uint8_t think_cd = 0;
    uint8_t ai_phase = 0;
    uint8_t ai_cd = 0;
    uint8_t squash_kind = 0;
    uint8_t squash_t = 0;
    uint8_t hit_vib_t = 0;
    uint8_t hit_vib_amp = 0;
    uint8_t hit_vib_max = 1;

    /* 状態異常・嫌がらせ・ボスフェーズ */
    uint16_t poison_t = 0;
    uint16_t burn_t = 0;
    uint16_t slow_t = 0;
    uint16_t haste_t = 0;
    uint16_t guard_t = 0;        /* Golem/Boss 反射棘ガード残り */
    uint8_t dot_cd = 0;          /* DoT ティックカウンタ */
    uint8_t boss_phase_mask = 0; /* bit0=50%, bit1=30%, bit2=10% */
    uint8_t phase_armor_t = 0;   /* ボスPhase切替咆哮アーマー */
};

static Actor allies[ALLY_N];
static Actor enemies[ENEMY_N];

constexpr float STAGE_W = FB_W * 3.0f; /* 1440px */
static float cam_x = 0.0f;
static uint32_t wave_n = 1;
static uint16_t wave_timer = 20;
static bool wave_active = false;
static uint32_t motion_tick = 0;
static uint8_t special_flash = 0;
static uint8_t shake = 0, shake_mag = 0;
static uint32_t rngs = 13371;

static DirectorMode dir_mode = DIR_BATTLE;
static uint16_t dir_timer = 0;
static uint16_t dir_total = 1;
static double win_bonus_shown = 0.0;
static float boss_hp_trail = 0.0f;
static float boss_down_x = 0.0f, boss_down_y = 0.0f;
static uint8_t active_boss_id = 0;

static char phase_banner_txt[36] = {};
static uint16_t phase_banner_t = 0;
static char cutin_who_name[16] = {};
static char cutin_name[24] = {};
static uint8_t cutin_who_atlas = 0;
static uint8_t cutin_who_tid = 0;
static uint16_t cutin_draw = 0;
static char flash_msg_buf[28] = {};
static uint16_t flash_frames = 0;
static uint32_t frame = 0;

/* ---------------- FX プール (96スロット) ---------------- */
enum FxKind : uint8_t {
    FX_DMG_NUM = 0,
    FX_DROP_TEXT = 1,
    FX_BOLT = 4,
    FX_CRYSTAL_RAIN = 6,
    FX_AQUA_STORM = 7,
    FX_SLAM_PILLAR = 8,
    FX_HEAL_NUM = 9,
    FX_SPR = 10,
    FX_TELEGRAPH = 11,
    FX_EBOLT = 12,     /* 敵呪い針(sub=0)/迫撃溶岩弾(sub=1)/凍弾(sub=2) */
    FX_HAZARD = 13,    /* layer=0 設置毒沼(sub=0)/火傷床(sub=1)/氷結床(sub=2) */
};

struct Fx {
    bool on;
    float x, y, z, vx, vy, vz;
    uint8_t kind;
    uint8_t sub;
    int16_t life;
    int16_t max_life;
    int32_t v;
    float dmg;
    int8_t src;
    int8_t dir;
    uint8_t spr_id;
    uint8_t nframes;
    uint8_t fdelay;
    uint8_t layer;        /* 0=地面/奥, 1=キャラ手前, 2=最前面数値 */
    uint8_t phys_mode;    /* 0=その場, 1=減速漂い, 2=宝石跳躍→HUD吸収, 3=紙吹雪, 4=上昇ハート */
    uint16_t scale_q8;
    float tx, ty;         /* 迫撃砲/誘導用目標座標 */
};
constexpr int FX_N = 96;
static Fx fxs[FX_N];
static uint8_t fx_i = 0;

static inline uint32_t rnd() { rngs ^= rngs << 13; rngs ^= rngs >> 17; rngs ^= rngs << 5; return rngs; }
static inline float frand(float a, float b) { return a + (rnd() % 1000) / 1000.0f * (b - a); }

static Fx* alloc_fx()
{
    for (auto& f : fxs) if (!f.on) return &f;
    for (auto& f : fxs) {
        if (f.kind == FX_SPR && (f.phys_mode == 3 || f.phys_mode == 1)) return &f;
    }
    for (auto& f : fxs) {
        if (f.kind == FX_SPR && f.phys_mode != 2) return &f;
    }
    for (auto& f : fxs) {
        if (f.kind != FX_DMG_NUM && f.kind != FX_DROP_TEXT && f.kind != FX_HEAL_NUM && f.kind != FX_HAZARD) return &f;
    }
    Fx* slot = &fxs[fx_i];
    fx_i = (fx_i + 1) % FX_N;
    return slot;
}

static void add_num_fx(float x, float y, float z, uint8_t kind, int32_t v, int16_t life, uint8_t sub = 0)
{
    Fx* slot = alloc_fx();
    *slot = Fx();
    slot->on = true; slot->kind = kind; slot->sub = sub;
    slot->layer = 2; /* 常に最前面で視認性を確保 */
    slot->x = x; slot->y = y; slot->z = z;
    slot->life = life; slot->max_life = life;
    slot->v = v;
}

static void spawn_vfx_sprite(float x, float y, float z, uint8_t spr_id, uint8_t nframes,
                             uint8_t fdelay, int8_t dir, uint8_t layer, uint8_t phys_mode,
                             bool play_in_freeze = false, uint16_t scale_q8 = 256)
{
    Fx* slot = alloc_fx();
    *slot = Fx();
    slot->on = true;
    slot->kind = FX_SPR;
    slot->x = x; slot->y = y; slot->z = z;
    slot->dir = dir >= 0 ? 1 : -1;
    slot->spr_id = spr_id;
    slot->nframes = nframes;
    slot->fdelay = fdelay ? fdelay : 1;
    slot->layer = layer;
    slot->phys_mode = phys_mode;
    (void)play_in_freeze;
    slot->scale_q8 = 256;
    (void)scale_q8;
    int16_t total = (int16_t)(nframes * slot->fdelay);
    slot->life = total;
    slot->max_life = total;
}

static void spawn_hazard(float x, float y, uint8_t sub, float dmg_per_tick, int16_t life)
{
    /* 描画・FPS保護のため設置床は最大6個に制限 (最古を再利用) */
    int cnt = 0;
    Fx* oldest = nullptr;
    for (auto& f : fxs) {
        if (f.on && f.kind == FX_HAZARD) {
            cnt++;
            if (!oldest || f.life < oldest->life) oldest = &f;
        }
    }
    Fx* slot = (cnt >= 6 && oldest) ? oldest : alloc_fx();
    *slot = Fx();
    slot->on = true;
    slot->kind = FX_HAZARD;
    slot->sub = sub; /* 0=acid, 1=burn, 2=frost */
    slot->layer = 0;
    if (x < 36.0f) x = 36.0f; if (x > STAGE_W - 36.0f) x = STAGE_W - 36.0f;
    if (y < BELT_Y0 + 12.0f) y = BELT_Y0 + 12.0f; if (y > BELT_Y1 - 12.0f) y = BELT_Y1 - 12.0f;
    slot->x = x; slot->y = y; slot->z = 0.0f;
    slot->vx = (sub == 1) ? 34.0f : 38.0f;
    slot->vy = 14.0f;
    slot->dmg = dmg_per_tick;
    slot->life = life;
    slot->max_life = life;
}

static void add_shake(uint8_t mag, uint8_t frames)
{
    if (mag < 3) return;
    uint32_t sp = gp::g.shake_pct;
    if (sp == 0) return;
    uint32_t m = (mag * sp + 50) / 100;
    if (m < 1) m = 1;
    if (m > 12) m = 12;
    if (m >= shake_mag) { shake_mag = (uint8_t)m; shake = frames; }
}

static inline void set_squash(Actor& a, uint8_t kind, uint8_t frames)
{
    a.squash_kind = kind;
    a.squash_t = frames;
}

/* ---------------- バランス曲線 (sim_roguelite_balance.py と1:1) ---------------- */
static void wave_scaling(uint32_t w, float& hp_mult, float& dmg_mult)
{
    float d = (w > 1) ? (float)(w - 1) : 0.0f;
    hp_mult  = (1.0f + d * 0.25f + d * d * 0.0190f) * (gp::g.enemy_hp_pct / 100.0f);
    dmg_mult = (1.0f + d * 0.115f + d * d * 0.0075f) * (gp::g.enemy_dmg_pct / 100.0f);
}

static int party_warden_pct()
{
    for (int i = 0; i < ALLY_N; ++i) {
        if (allies[i].alive && allies[i].state != A_DOWN &&
            game::TALENTS[allies[i].talent_id].trait == game::TRAIT_WARDEN) {
            return 35;
        }
    }
    return 0;
}

static int party_scavenger_pct()
{
    for (int i = 0; i < ALLY_N; ++i) {
        if (allies[i].alive && allies[i].state != A_DOWN &&
            game::TALENTS[allies[i].talent_id].trait == game::TRAIT_SCAVENGER) {
            return 20;
        }
    }
    return 0;
}

static float effective_ally_hpmax(uint8_t tid, uint8_t rank)
{
    const auto& td = game::TALENTS[tid % game::TALENT_N];
    float rm = 1.0f + 0.28f * (float)(rank > 0 ? rank - 1 : 0);
    return td.hp * rm * (game::st.mods.hp_pct / 100.0f) * (gp::g.hp_pct / 100.0f);
}

/* ---------------- 初期化 & パーティ同期 ---------------- */
static void init_ram_assets();

void sync_party(bool reset_pos)
{
    float anchor_x = STAGE_W * 0.5f - 28.0f;
    for (int i = 0; i < ALLY_N; ++i) {
        if (allies[i].alive) { anchor_x = allies[i].x; break; }
    }
    if (anchor_x < 60.0f) anchor_x = 60.0f;
    if (anchor_x > STAGE_W - 80.0f) anchor_x = STAGE_W - 80.0f;

    for (int i = 0; i < ALLY_N; ++i) {
        const auto& slot = game::st.party[i];
        Actor& a = allies[i];
        if (!slot.active) {
            a = Actor();
            a.alive = false;
            a.ally = true;
            continue;
        }
        const auto& td = game::TALENTS[slot.talent_id % game::TALENT_N];
        float new_hpmax = effective_ally_hpmax(slot.talent_id, slot.rank);
        bool was_alive = a.alive;
        bool same_hero = was_alive && (a.talent_id == slot.talent_id);
        bool ranked_up = same_hero && (slot.rank > a.rank);

        if (!was_alive || reset_pos) {
            a = Actor();
            a.alive = true;
            a.ally = true;
            a.x = reset_pos ? (STAGE_W * 0.5f - 36.0f + i * 32.0f) : (anchor_x - i * 28.0f);
            a.y = (i == 0) ? 222.0f : (i == 1 ? 174.0f : 276.0f);
            a.home_x = a.x;
            a.dir = 1;
            a.hpmax = new_hpmax;
            a.hp = new_hpmax;
            a.rage = (float)game::st.mods.start_mp;
            if (a.rage > 100.0f) a.rage = 100.0f;
        } else {
            float old_max = a.hpmax > 1.0f ? a.hpmax : new_hpmax;
            a.hpmax = new_hpmax;
            if (!same_hero) {
                /* 入れ替え加入は全回復で参戦 */
                a.hp = new_hpmax;
                if (a.state == A_DOWN) a.state = A_IDLE;
                a.poison_t = a.burn_t = a.slow_t = 0;
            } else {
                a.hp = a.hp * new_hpmax / old_max;
                if (ranked_up) {
                    if (a.state == A_DOWN) a.state = A_IDLE;
                    a.hp = fminf(a.hpmax, a.hp + a.hpmax * 0.35f);
                }
            }
        }
        a.talent_id = slot.talent_id;
        a.base_atlas = td.base_atlas;
        a.rank = slot.rank;
        a.use_orig_pal = (td.use_orig_pal != 0);
        a.pal_pri = td.pal_pri;
        a.pal_sec = td.pal_sec;
        a.windup_f = td.windup;
        a.recover_f = td.recover;
        a.type = td.style;
    }
    hud_st[0].valid = false;
    hud_st[1].valid = false;
}

void init()
{
    rngs = esp_random() | 1u;
    memset(allies, 0, sizeof(allies));
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));

    sync_party(true);
    cam_x = STAGE_W * 0.5f - FB_W * 0.5f;

    if (!band_buf) {
        band_buf = (uint16_t*)heap_caps_malloc(FB_W * BAND_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!band_buf) band_buf = (uint16_t*)heap_caps_malloc(FB_W * BAND_H * 2, MALLOC_CAP_SPIRAM);
    }
    if (!sky_buf) {
        sky_buf = (uint16_t*)heap_caps_malloc(scenery::SKY_W * scenery::SKY_H * 2, MALLOC_CAP_SPIRAM);
        if (sky_buf) {
            for (int i = 0; i < scenery::SKY_W * scenery::SKY_H; ++i)
                sky_buf[i] = scenery::SKY_PAL[scenery::SKY_IDX[i]];
        }
    }
    if (!gnd_buf) {
        gnd_buf = (uint16_t*)heap_caps_malloc(scenery::GND_W * scenery::GND_H * 2, MALLOC_CAP_SPIRAM);
        if (gnd_buf) {
            for (int i = 0; i < scenery::GND_W * scenery::GND_H; ++i)
                gnd_buf[i] = scenery::GND_PAL[scenery::GND_IDX[i]];
        }
    }
    init_ram_assets();

    wave_n = (game::st.wave_reached >= 1 && game::st.wave_reached <= 50) ? game::st.wave_reached : 1;
    wave_timer = 15;
    wave_active = false;
    dir_mode = DIR_BATTLE;
    dir_timer = 0; dir_total = 1;
    boss_hp_trail = 0.0f;
    special_flash = 0; shake = 0; shake_mag = 0;
    cutin_draw = 0; flash_frames = 0; phase_banner_t = 0;
    invalidate_all_bands();
    hud_st[0].valid = false;
    hud_st[1].valid = false;
}

void set_wave(uint32_t w)
{
    if (w < 1) w = 1;
    if (w > 50) w = 50;
    wave_n = w;
    game::st.wave_reached = w;
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    wave_timer = 15;
    wave_active = false;
    dir_mode = DIR_BATTLE;
    dir_timer = 0;
    boss_hp_trail = 0.0f;
    cutin_draw = 0; special_flash = 0; shake = 0; shake_mag = 0; phase_banner_t = 0;
    for (auto& a : allies) {
        a.squash_t = 0; a.hit_vib_t = 0; a.flash = 0;
        a.poison_t = a.burn_t = a.slow_t = 0;
    }
    invalidate_all_bands();
    hud_st[0].valid = false;
    hud_st[1].valid = false;
}
uint32_t wave() { return wave_n; }

void heal_all()
{
    for (int i = 0; i < ALLY_N; i++) {
        Actor& a = allies[i];
        if (!a.alive) continue;
        a.hp = a.hpmax;
        a.poison_t = a.burn_t = a.slow_t = 0;
        if (a.state == A_DOWN || a.state == A_VICTORY) {
            a.state = A_IDLE;
            if (a.x < cam_x + 40.0f || a.x > cam_x + FB_W - 40.0f) {
                a.x = cam_x + 90.0f + i * 36.0f;
            }
            if (a.x < 24.0f) a.x = 24.0f;
            if (a.x > STAGE_W - 24.0f) a.x = STAGE_W - 24.0f;
            a.home_x = a.x;
        }
        a.z = 0.0f; a.vz = 0.0f; a.vx = 0.0f; a.vy = 0.0f;
        a.invuln = true; a.invuln_t = 36;
    }
}

bool  ally_active(int i) { return (i >= 0 && i < ALLY_N) ? allies[i].alive : false; }
float ally_hp(int i)     { return (i >= 0 && i < ALLY_N) ? allies[i].hp : 0.0f; }
float ally_hpmax(int i)  { return (i >= 0 && i < ALLY_N) ? allies[i].hpmax : 1.0f; }
float ally_rage(int i)   { return (i >= 0 && i < ALLY_N) ? allies[i].rage : 0.0f; }

void flash_message(const char* msg, uint16_t frames)
{
    snprintf(flash_msg_buf, sizeof(flash_msg_buf), "%s", msg);
    flash_frames = frames;
}

void request_full_restore()
{
    invalidate_all_bands();
    hud_st[0].valid = false;
    hud_st[1].valid = false;
}

void on_pet()
{
    for (int i = 0; i < ALLY_N; ++i) {
        Actor& a = allies[i];
        if (!a.alive) continue;
        if (a.state != A_DOWN && a.hp < a.hpmax) {
            float heal = a.hpmax * 0.14f;
            a.hp = fminf(a.hpmax, a.hp + heal);
            add_num_fx(a.x + frand(-6, 6), a.y - 44, a.z, FX_HEAL_NUM, (int32_t)heal, 28);
        }
        spawn_vfx_sprite(a.x + frand(-8, 8), a.y - 48, a.z,
                         vfx_sprites::FXS_HEART_BUFF, 1, 32, 1, 1, 4, true, 256);
        if (i == 0 && a.state != A_DOWN && a.z <= 0.0f) {
            a.z = 1.0f; a.vz = 3.4f;
            set_squash(a, 1, 6);
        }
    }
}

void on_draft_pick(int pick_result)
{
    sync_party(false);
    for (auto& a : allies) {
        if (!a.alive || a.state == A_DOWN) continue;
        spawn_vfx_sprite(a.x, a.y - 6, a.z, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 200);
        spawn_vfx_sprite(a.x, a.y - 26, a.z, vfx_sprites::FXS_MIRA_BURST_0, 3, 3, 1, 1, 0, true, 256);
    }
    if (pick_result == 1) flash_message("TALENT HIRED!", 48);
    else if (pick_result == 2) flash_message("RANK UP!", 48);
    else if (pick_result == 3) flash_message("PARTY SWAPPED!", 48);
    else if (pick_result == 4) flash_message("CHARM EQUIPPED!", 48);
}

static bool has_saved_progress()
{
    return game::st.wave_reached > 1 || game::st.boxes_opened > 0 ||
           game::st.crystals >= 1.0 || game::st.run_number > 1 ||
           game::st.total_clears > 0 || game::party_count() > 1;
}

bool is_title()
{
    return dir_mode == DIR_TITLE;
}

void enter_title()
{
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    sync_party(true);
    cam_x = 120.0f;
    for (int i = 0; i < ALLY_N; ++i) {
        Actor& a = allies[i];
        if (!a.alive) continue;
        a.x = cam_x + 240.0f - i * 44.0f;
        a.y = 224.0f + ((i == 1) ? -14.0f : (i == 2 ? 14.0f : 0.0f));
        a.z = 0.0f; a.vz = 0.0f; a.vx = 0.0f; a.vy = 0.0f;
        a.dir = 1;
        a.state = A_WALK;
        a.home_x = a.x;
    }
    wave_active = false;
    wave_timer = 18;
    dir_mode = DIR_TITLE;
    dir_timer = 0;
    dir_total = 1;
    cutin_draw = 0;
    special_flash = 0;
    shake = 0; shake_mag = 0;
    flash_frames = 0;
    phase_banner_t = 0;
    request_full_restore();
}

static constexpr uint16_t OP_DURATION = 270;

static void enter_opening(bool new_run)
{
    if (new_run) {
        game::start_new_run();
    }
    wave_n = 1;
    game::st.wave_reached = 1;
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    sync_party(true);
    cam_x = 140.0f;
    for (int i = 0; i < ALLY_N; ++i) {
        Actor& a = allies[i];
        if (!a.alive) continue;
        a.x = cam_x + 170.0f - i * 42.0f;
        a.y = 228.0f + ((i == 1) ? -14.0f : (i == 2 ? 14.0f : 0.0f));
        a.z = 0.0f; a.vz = 0.0f; a.vx = 0.0f; a.vy = 0.0f;
        a.dir = 1;
        a.state = A_WALK;
        a.home_x = a.x;
    }
    wave_active = false;
    wave_timer = 18;
    dir_mode = DIR_OPENING;
    dir_timer = 0;
    dir_total = OP_DURATION;
    cutin_draw = 0;
    special_flash = 0;
    shake = 0; shake_mag = 0;
    flash_frames = 0;
    phase_banner_t = 0;
    request_full_restore();
}

static void finish_opening_to_wave1()
{
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    dir_mode = DIR_BATTLE;
    dir_timer = 0;
    dir_total = 1;
    wave_n = 1;
    game::st.wave_reached = 1;
    wave_active = false;
    wave_timer = 15;
    sync_party(true);
    cam_x = STAGE_W * 0.5f - FB_W * 0.5f;
    special_flash = 5;
    for (auto& a : allies) {
        if (!a.alive) continue;
        spawn_vfx_sprite(a.x, a.y - 6, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 200);
        spawn_vfx_sprite(a.x, a.y - 26, 0.0f, vfx_sprites::FXS_MIRA_BURST_0, 3, 3, 1, 1, 0, true, 256);
    }
    char msg[28];
    snprintf(msg, sizeof(msg), "RUN #%u  WAVE 1!", (unsigned)game::st.run_number);
    flash_message(msg, 48);
    request_full_restore();
}

void force_opening()
{
    enter_opening(false);
    Serial.println("[cmd] started OP animation");
}

static void leave_title_to_battle(bool new_run)
{
    if (new_run || !has_saved_progress()) {
        enter_opening(new_run);
        return;
    }
    dir_mode = DIR_BATTLE;
    dir_timer = 0;
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    sync_party(true);
    cam_x = STAGE_W * 0.5f - FB_W * 0.5f;
    wave_timer = 18;
    wave_active = false;
    char wmsg[28];
    snprintf(wmsg, sizeof(wmsg), "WAVE %u START!", (unsigned)wave_n);
    flash_message(wmsg, 48);
    special_flash = 5;
    for (auto& a : allies) {
        if (!a.alive) continue;
        spawn_vfx_sprite(a.x, a.y - 6, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 200);
        spawn_vfx_sprite(a.x, a.y - 26, 0.0f, vfx_sprites::FXS_MIRA_BURST_0, 3, 3, 1, 1, 0, true, 256);
    }
    request_full_restore();
}

bool is_modal_director()
{
    return dir_mode == DIR_TITLE || dir_mode == DIR_OPENING || dir_mode == DIR_GAME_OVER || dir_mode == DIR_ENDING;
}

bool notify_tap(int x, int y)
{
    if (dir_mode == DIR_TITLE) {
        if (dir_timer < 10) return false;
        int by = y - 56;
        if (has_saved_progress()) {
            /* NEW RUN ボタン (x: 296..460, battle y: 264..330) */
            if (x >= 296 && x < 460 && by >= 264 && by < 330) {
                leave_title_to_battle(true);
                return true;
            }
        }
        leave_title_to_battle(false);
        return true;
    }
    if (dir_mode == DIR_OPENING) {
        if (dir_timer >= 12) {
            finish_opening_to_wave1();
            return true;
        }
        return false;
    }
    if ((dir_mode == DIR_GAME_OVER || dir_mode == DIR_ENDING) && dir_timer >= 36) {
        game::start_new_run();
        init();
        char rmsg[24];
        snprintf(rmsg, sizeof(rmsg), "RUN #%u START!", (unsigned)game::st.run_number);
        flash_message(rmsg, 45);
        return true;
    }
    if (dir_mode == DIR_BOSS_INTRO) {
        for (auto& e : enemies) if (e.alive && e.type == E_BOSS) { e.z = 0; e.vz = 0; }
        dir_mode = DIR_BATTLE;
        dir_timer = 0;
    } else if (dir_mode == DIR_BOSS_VICTORY && (dir_total - dir_timer) >= 24) {
        dir_timer = 1;
    } else if (dir_mode == DIR_WAVE_CLEAR && (dir_total - dir_timer) >= 18) {
        dir_timer = 1;
    }
    return false;
}

void dump()
{
    Serial.printf("[dump] Run#%u Wave=%u/50 Clears=%u Crystals=%.0f\r\n",
                  (unsigned)game::st.run_number, (unsigned)wave_n, (unsigned)game::st.total_clears, game::st.crystals);
    for (int i = 0; i < ALLY_N; ++i) {
        const auto& a = allies[i];
        if (!a.alive) { Serial.printf("  slot%d EMPTY\r\n", i); continue; }
        Serial.printf("  slot%d %-8s *%u x=%.0f y=%.0f hp=%.0f/%.0f rage=%.0f st=%u\r\n",
                      i, game::TALENTS[a.talent_id].name, (unsigned)a.rank, a.x, a.y, a.hp, a.hpmax, a.rage, a.state);
    }
    for (const auto& e : enemies) {
        if (!e.alive) continue;
        Serial.printf("  enemy type=%u sub=%u x=%.0f y=%.0f hp=%.0f/%.0f\r\n",
                      e.type, e.sub_kind, e.x, e.y, e.hp, e.hpmax);
    }
}

/* ---------------- スポーン & ウェーブ開始 ---------------- */
static Actor* free_enemy()
{
    for (auto& e : enemies) if (!e.alive) return &e;
    return nullptr;
}

static Actor* spawn_enemy(int type, float x_hint, float y_hint = -1.0f, uint8_t force_sub = 255)
{
    Actor* e = free_enemy();
    if (!e) return nullptr;
    float hp_mult = 1.0f, dmg_mult = 1.0f;
    wave_scaling(wave_n, hp_mult, dmg_mult);
    (void)dmg_mult;

    *e = Actor();
    e->alive = true;
    e->ally = false;
    e->type = (uint8_t)type;

    float hpbase = 36.0f;
    uint8_t sub = (force_sub != 255) ? force_sub : (uint8_t)(rnd() % 3);

    if (type == E_SLIME) {
        hpbase = 36.0f;
        e->base_atlas = 0;
        e->sub_kind = (force_sub != 255) ? (force_sub % 3) : (uint8_t)(rnd() % (wave_n >= 3 ? 3 : 2));
        if (e->sub_kind == 0) { /* Acid Spitter (元色黄緑) */
            e->use_orig_pal = true;
        } else if (e->sub_kind == 1) { /* Leech (紫桃) */
            e->use_orig_pal = false; e->pal_pri = RGB(195, 70, 215); e->pal_sec = RGB(255, 130, 210);
        } else { /* Bomber (溶岩橙赤) */
            e->use_orig_pal = false; e->pal_pri = RGB(240, 85, 45); e->pal_sec = RGB(255, 220, 80);
            hpbase = 32.0f;
        }
        e->windup_f = 14; e->recover_f = 18;
    } else if (type == E_BAT) {
        hpbase = 28.0f;
        e->base_atlas = 1;
        e->sub_kind = (force_sub != 255) ? (force_sub % 2) : (uint8_t)(rnd() % 2);
        if (e->sub_kind == 0) { /* Curse Sniper */
            e->use_orig_pal = true;
        } else { /* Blood Diver */
            e->use_orig_pal = false; e->pal_pri = RGB(215, 55, 85); e->pal_sec = RGB(255, 210, 90);
        }
        e->windup_f = 14; e->recover_f = 18;
    } else if (type == E_GOLEM) {
        hpbase = 86.0f;
        e->base_atlas = 2;
        e->sub_kind = (force_sub != 255) ? (force_sub % 2) : (uint8_t)(rnd() % 2);
        if (e->sub_kind == 0) { /* Magma Mortar */
            e->use_orig_pal = false; e->pal_pri = RGB(210, 105, 55); e->pal_sec = RGB(255, 215, 70);
        } else { /* Thorn Guard */
            e->use_orig_pal = true;
        }
        e->windup_f = 20; e->recover_f = 18;
    } else if (type == E_BOSS) {
        uint8_t bid = game::boss_id_for_wave(wave_n);
        active_boss_id = bid;
        const auto& bd = game::BOSSES[bid];
        hpbase = (float)bd.hp_base * 0.90f * ((wave_n == 50) ? 1.22f : 1.0f);
        e->talent_id = bid;
        e->base_atlas = (bd.chassis == 1) ? 2 : 3;
        e->use_orig_pal = (bd.use_orig_pal != 0);
        e->pal_pri = bd.pal_pri;
        e->pal_sec = bd.pal_sec;
        e->windup_f = 16; e->recover_f = 16;
    }

    float hpmax = hpbase * hp_mult;
    float sx = x_hint;
    if (sx < 54.0f) sx = 54.0f;
    if (sx > STAGE_W - 54.0f) sx = STAGE_W - 54.0f;
    e->x = sx;
    e->y = (y_hint > 0.0f) ? y_hint : frand(BELT_Y0 + 16, BELT_Y1 - 14);
    e->z = (type == E_BAT) ? 16.0f : 0.0f;
    e->hpmax = hpmax; e->hp = hpmax;
    e->dir = (sx > cam_x + FB_W * 0.5f) ? -1 : 1;
    e->state = A_WALK;
    e->spawn_t = (type == E_BOSS) ? 0 : 14;
    if (type == E_BOSS) boss_hp_trail = hpmax;
    return e;
}

static void apply_wave_start_buffs()
{
    for (int i = 0; i < ALLY_N; ++i) {
        Actor& a = allies[i];
        if (!a.alive || a.state == A_DOWN) continue;
        float sh_pct = (float)game::st.mods.shield_pct;
        if (game::TALENTS[a.talent_id].trait == game::TRAIT_BARRIER) sh_pct += 18.0f;
        if (sh_pct > 0.0f) {
            a.shield = a.hpmax * (sh_pct / 100.0f);
        } else {
            a.shield = 0.0f;
        }
        if (game::st.mods.start_mp > 0 && a.rage < (float)game::st.mods.start_mp) {
            a.rage = fminf(100.0f, (float)game::st.mods.start_mp);
        }
    }
}

static void start_wave()
{
    wave_active = true;
    apply_wave_start_buffs();

    float cx = 0.0f; int cnt = 0;
    for (auto& a : allies) if (a.alive) { cx += a.x; cnt++; }
    cx = cnt ? (cx / cnt) : (STAGE_W * 0.5f);
    int main_dir = (cx < STAGE_W * 0.5f) ? 1 : -1;

    bool boss_wave = (wave_n % 5 == 0);
    int n = 3 + (int)(wave_n / 4);
    if (n > 6) n = 6;
    if (boss_wave) {
        Actor* b = spawn_enemy(E_BOSS, cx + main_dir * 220.0f, (BELT_Y0 + BELT_Y1) * 0.52f);
        if (b) {
            b->z = 210.0f;
            b->vz = 0.0f;
            b->state = A_SPECIAL;
            b->timer = 40;
            dir_mode = DIR_BOSS_INTRO;
            uint32_t cf = gp::g.cutin_frames;
            if (cf < 36) cf = 36;
            if (cf > 180) cf = 180;
            dir_total = (uint16_t)cf;
            dir_timer = dir_total;
        }
        n = 2;
    }
    for (int i = 0; i < n; i++) {
        int t = E_SLIME;
        uint32_t r = rnd() % 100;
        if (wave_n >= 2 && r < 34) t = E_BAT;
        if (wave_n >= 3 && r >= 34 && r < 56) t = E_GOLEM;
        bool flank = (i >= 2 && (i % 3 == 2) && cx > 240.0f && cx < STAGE_W - 240.0f);
        int side = flank ? -main_dir : main_dir;
        float dist = flank ? (160.0f + frand(0, 60)) : (180.0f + (float)(i * 42) + frand(0, 35));
        spawn_enemy(t, cx + side * dist);
    }
}

/* ---------------- デバッグ強制トリガ ---------------- */
void force_boss()
{
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    if (wave_n % 5 != 0) wave_n = ((wave_n / 5) + 1) * 5;
    if (wave_n > 50) wave_n = 50;
    game::st.wave_reached = wave_n;
    heal_all();
    wave_active = false;
    dir_mode = DIR_BATTLE;
    dir_timer = 0; phase_banner_t = 0;
    cutin_draw = 0; special_flash = 0; shake = 0; shake_mag = 0;
    for (auto& a : allies) { a.squash_t = 0; a.hit_vib_t = 0; a.flash = 0; }
    invalidate_all_bands();
    start_wave();
    Serial.printf("[cmd] forced BOSS %s wave %u\r\n", game::BOSSES[active_boss_id].name2, (unsigned)wave_n);
}

void force_ult()
{
    if (!wave_active) start_wave();
    for (int i = 0; i < ALLY_N; ++i) {
        if (!allies[i].alive || allies[i].state == A_DOWN) continue;
        allies[i].rage = 100.0f;
        if (i == 0) { allies[i].state = A_IDLE; allies[i].timer = 0; }
    }
    Serial.println("[cmd] forced ULT");
}

/* ---------------- 物理 & ダメージ & ボス 50/30/10% Phase ---------------- */
static void rage_add(Actor& a, float v)
{
    float mult = (game::st.mods.rage_pct / 100.0f) * (gp::g.rage_pct / 100.0f);
    if (game::TALENTS[a.talent_id].trait == game::TRAIT_RAGE) mult *= 1.30f;
    a.rage += v * mult;
    if (a.rage > 100.0f) a.rage = 100.0f;
}

/* ---------------- 統一 Z 軸物理定数 (浮遊・画面外飛散防止) ---------------- */
static constexpr float GRAV_GROUND_UNIT = 0.44f;
static constexpr float GRAV_AIR_JUGGLE  = 0.36f;
static constexpr float GRAV_SLIME_HOP   = 0.48f;
static constexpr float MAX_ACTOR_Z      = 92.0f;
static constexpr float MAX_LIFT_VZ      = 6.2f;

static inline float enemy_gravity(const Actor& e)
{
    if (e.type == E_BAT) return (e.z > 32.0f) ? 0.24f : 0.0f;
    if (e.state == A_HURT) return GRAV_AIR_JUGGLE;
    if (e.type == E_SLIME) return GRAV_SLIME_HOP;
    return GRAV_GROUND_UNIT;
}

static inline void integrate_vel(Actor& a, float min_x, float max_x, float damp = 0.85f, float grav = GRAV_GROUND_UNIT)
{
    a.x += a.vx; a.y += a.vy;
    a.vx *= damp; a.vy *= damp;
    if (a.x < min_x) { a.x = min_x; if (a.vx < 0) a.vx = 0; }
    if (a.x > max_x) { a.x = max_x; if (a.vx > 0) a.vx = 0; }
    if (a.y < BELT_Y0) { a.y = BELT_Y0; if (a.vy < 0) a.vy = 0; }
    if (a.y > BELT_Y1) { a.y = BELT_Y1; if (a.vy > 0) a.vy = 0; }

    if (a.z > 0.0f || a.vz != 0.0f) {
        a.z += a.vz;
        a.vz -= grav;
        if (a.z > MAX_ACTOR_Z) {
            a.z = MAX_ACTOR_Z;
            if (a.vz > 0.0f) a.vz = 0.0f;
        }
        if (a.z <= 0.0f) {
            if (!a.ally && a.vz < -3.4f) {
                a.z = 0.5f;
                a.vz = -a.vz * 0.30f;
                set_squash(a, 2, 5);
                spawn_vfx_sprite(a.x, a.y - 2, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3, -a.dir, 0, 1);
            } else {
                if (a.vz < -2.4f) {
                    set_squash(a, 2, 5);
                    spawn_vfx_sprite(a.x, a.y - 2, 0.0f, vfx_sprites::FXS_DUST_0, 3, 2, -a.dir, 0, 1);
                }
                a.z = 0.0f; a.vz = 0.0f; a.air_combo = 0;
            }
        }
    }
}

static void spawn_bolt(int ally_idx, const Actor* t, float dmg, float vy_off, uint8_t bolt_mode = 0)
{
    const Actor& a = allies[ally_idx];
    Fx* slot = alloc_fx();
    *slot = Fx();
    slot->on = true;
    slot->x = a.x + a.dir * 16;
    slot->y = a.y - 22 + vy_off;
    slot->z = a.z + 6.0f;
    float spd = (bolt_mode == 1) ? 10.5f : 7.8f;
    slot->vx = ((t && t->x > a.x) ? 1 : (t ? -1 : a.dir)) * spd;
    slot->vy = vy_off * 0.14f;
    slot->kind = FX_BOLT;
    slot->sub = bolt_mode; /* 0=誘導弾, 1=高速貫通ランス */
    slot->layer = 1;
    slot->dir = (slot->vx >= 0) ? 1 : -1;
    slot->life = (bolt_mode == 1) ? 38 : 55;
    slot->max_life = slot->life;
    slot->dmg = dmg;
    slot->src = (int8_t)ally_idx;
    slot->v = 0;
    if (t) {
        for (int i = 0; i < ENEMY_N; i++) if (&enemies[i] == t) { slot->v = i; break; }
    }
}

static void spawn_enemy_bolt(float x, float y, float z, float vx, float vy, float dmg,
                             uint8_t sub, float tx = 0, float ty = 0)
{
    int cnt = 0;
    Fx* oldest = nullptr;
    for (auto& f : fxs) {
        if (f.on && f.kind == FX_EBOLT) {
            cnt++;
            if (!oldest || f.life < oldest->life) oldest = &f;
        }
    }
    Fx* slot = (cnt >= 8 && oldest) ? oldest : alloc_fx();
    *slot = Fx();
    slot->on = true;
    slot->kind = FX_EBOLT;
    slot->sub = sub; /* 0=呪い針(スロウ), 1=迫撃砲, 2=氷結弾 */
    slot->layer = 1;
    slot->x = x; slot->y = y; slot->z = z;
    slot->vx = vx; slot->vy = vy;
    slot->vz = (sub == 1) ? 4.6f : 0.0f;
    slot->tx = tx; slot->ty = ty;
    slot->dmg = dmg;
    slot->dir = (vx >= 0) ? 1 : -1;
    slot->life = (sub == 1) ? 44 : 68;
    slot->max_life = slot->life;
}

static void spawn_crystal_gems(float x, float y, float z, int count)
{
    for (int i = 0; i < count; ++i) {
        Fx* f = alloc_fx();
        *f = Fx();
        f->on = true;
        f->kind = FX_SPR;
        f->spr_id = vfx_sprites::FXS_CRYSTAL_GEM_0;
        f->nframes = 2; f->fdelay = 4;
        f->layer = 1; f->phys_mode = 2;
        f->x = x + frand(-10, 10); f->y = y + frand(-6, 6); f->z = z + 14.0f;
        f->vx = frand(-3.2f, 3.2f); f->vy = frand(-1.0f, 1.0f); f->vz = frand(3.6f, 6.8f);
        f->life = 68; f->max_life = 68; f->scale_q8 = 256;
    }
}

static void spawn_confetti_burst(float x, float y, float z, int count)
{
    for (int i = 0; i < count; ++i) {
        Fx* f = alloc_fx();
        *f = Fx();
        f->on = true;
        f->kind = FX_SPR;
        f->spr_id = (uint8_t)(vfx_sprites::FXS_CONFETTI_0 + (rnd() % 4));
        f->nframes = 1; f->fdelay = 1;
        f->layer = 1; f->phys_mode = 3;
        f->x = x + frand(-24, 24); f->y = y + frand(-10, 10); f->z = z + frand(10, 42);
        f->vx = frand(-4.5f, 4.5f); f->vy = frand(-1.2f, 1.2f); f->vz = frand(3.0f, 7.5f);
        f->v = (int32_t)(rnd() % 100);
        f->life = (int16_t)(75 + (rnd() % 45));
        f->max_life = f->life; f->scale_q8 = 256;
    }
}

static void hurt_ally(Actor& a, float dmg, int8_t dir, bool is_chip = false, uint8_t ailment = 0);

static void heal_ally_direct(Actor& a, float amt, bool show_num = true)
{
    if (!a.alive || a.state == A_DOWN || amt <= 0.0f) return;
    a.hp = fminf(a.hpmax, a.hp + amt);
    if (show_num && (int)amt >= 1) {
        add_num_fx(a.x + frand(-4, 4), a.y - 54, a.z, FX_HEAL_NUM, (int32_t)amt, 26);
    }
}

static void hit_enemy(Actor& e, float dmg, int8_t dir, bool big, int src_ally, float lift_vz = 0.0f)
{
    if (!e.alive) return;
    if (e.z > 2.0f || (src_ally >= 0 && allies[src_ally].z > 2.0f)) {
        dmg *= gp::g.air_pct / 100.0f;
    }
    if (e.type == E_BOSS && game::st.mods.boss_slay_pct > 0) {
        dmg *= 1.0f + game::st.mods.boss_slay_pct / 100.0f;
    }
    if (e.hp < e.hpmax * 0.40f && game::st.mods.exec_pct > 0) {
        dmg *= 1.0f + game::st.mods.exec_pct / 100.0f;
    }
    int crit_chance = game::st.mods.crit_pct + (int)gp::g.crit_pct - 5;
    if (src_ally >= 0 && game::TALENTS[allies[src_ally].talent_id].trait == game::TRAIT_CRIT) crit_chance += 15;
    if (src_ally >= 0 && allies[src_ally].type == game::STYLE_FLURRY_DART) crit_chance += 8;
    if (crit_chance > 95) crit_chance = 95;
    bool crit = ((int)(rnd() % 100) < crit_chance);
    if (crit) dmg *= 2.0f;

    /* Golem/Boss 棘ガード反撃 + ボス咆哮アーマー */
    if (e.type == E_BOSS && e.phase_armor_t > 0) dmg *= 0.45f;
    if (e.guard_t > 0 && src_ally >= 0 && allies[src_ally].type != game::STYLE_MAGIC_BOLT &&
        allies[src_ally].type != game::STYLE_SPREAD_VOLLEY && allies[src_ally].type != game::STYLE_SNIPER_LANCE) {
        hurt_ally(allies[src_ally], fmaxf(2.0f, dmg * 0.20f), -dir, true);
    }

    float new_hp = e.hp - dmg;
    /* ボス 50% / 30% / 10% しきい値を一撃で飛ばさない安全クランプ */
    if (e.type == E_BOSS && e.hpmax > 0.0f) {
        if (!(e.boss_phase_mask & 0x01) && new_hp < e.hpmax * 0.485f) new_hp = e.hpmax * 0.485f;
        else if ((e.boss_phase_mask & 0x01) && !(e.boss_phase_mask & 0x02) && new_hp < e.hpmax * 0.285f) new_hp = e.hpmax * 0.285f;
        else if ((e.boss_phase_mask & 0x02) && !(e.boss_phase_mask & 0x04) && new_hp < e.hpmax * 0.085f) new_hp = e.hpmax * 0.085f;
    }

    float dealt = e.hp - new_hp;
    if (dealt < 0.0f) dealt = 0.0f;
    e.hp = new_hp;
    e.flash = 3;
    bool heavy = big || crit;

    /* 凍結・スロウ付与 */
    if (game::st.mods.frost_slow > 0 && ((int)(rnd() % 100) < game::st.mods.frost_slow)) {
        e.slow_t = 90;
    } else if (src_ally >= 0 && game::TALENTS[allies[src_ally].talent_id].trait == game::TRAIT_FROST) {
        e.slow_t = 80;
    }

    if (e.type == E_BOSS && !heavy) {
        e.vx += dir * 0.75f;
    } else if (e.phase_armor_t == 0) {
        e.state = A_HURT;
        e.timer = (e.type == E_BOSS) ? 5 : (heavy ? 11 : 8);
        e.vx = dir * ((e.type == E_BOSS) ? 2.1f : (heavy ? 5.6f : 2.6f));
        e.vy = frand(-1.1f, 1.1f);
        set_squash(e, 3, 5);
        float lv = (e.type == E_BOSS) ? (lift_vz * 0.42f) : lift_vz;
        if (lv > 0.0f) {
            float damp_z = fmaxf(0.25f, 1.0f - e.z / MAX_ACTOR_Z);
            lv = fminf(MAX_LIFT_VZ, lv * damp_z);
        }
        if (lv != 0.0f) {
            e.vz = lv;
            if (lv > 0.0f && e.z < 1.0f) e.z = 1.0f;
        } else if (e.z > 0.0f && e.z < 68.0f && e.type != E_BOSS) {
            e.vz = fmaxf(0.6f, 2.2f - e.z * 0.028f);
        }
    }

    e.hit_vib_t = heavy ? 8 : 5;
    e.hit_vib_max = e.hit_vib_t;
    e.hit_vib_amp = heavy ? 6 : 3;

    float hx = e.x - dir * 6.0f + frand(-4, 4);
    float hy = e.y - 24.0f + frand(-4, 4);
    uint32_t hfx = gp::g.hit_fx_frames > 20 ? 20 : gp::g.hit_fx_frames;
    uint8_t fd = (uint8_t)((hfx + 2) / 4);
    if (fd < 1) fd = 1;
    spawn_vfx_sprite(hx, hy, e.z, heavy ? vfx_sprites::FXS_HIT_HEAVY_0 : vfx_sprites::FXS_HIT_LIGHT_0, 4, fd, dir, 1, 0, true);

    float num_y = e.y - ((e.type == E_BOSS) ? 76.0f : (e.type == E_GOLEM ? 60.0f : 48.0f));
    add_num_fx(e.x + frand(-8, 8), num_y, e.z, FX_DMG_NUM, (int32_t)fmaxf(1.0f, dealt), crit ? 38 : 30, crit ? 1 : 0);

    if (src_ally >= 0) {
        Actor& sa = allies[src_ally];
        rage_add(sa, 7.5f);
        float vamp = (game::st.mods.vamp_x10 + (int)gp::g.vamp_x10) / 10.0f;
        if (game::TALENTS[sa.talent_id].trait == game::TRAIT_VAMP) vamp += 2.5f;
        if (vamp > 0.0f && sa.alive && sa.state != A_DOWN && sa.hp < sa.hpmax) {
            heal_ally_direct(sa, vamp, (rnd() % 2 == 0));
        }
        /* 連鎖スパーク */
        if (game::st.mods.chain_spark > 0 && ((int)(rnd() % 100) < game::st.mods.chain_spark)) {
            for (auto& o : enemies) {
                if (&o != &e && o.alive && fabsf(o.x - e.x) < 120.0f) {
                    o.hp -= dealt * 0.45f;
                    o.flash = 2;
                    spawn_vfx_sprite(o.x, o.y - 22, o.z, vfx_sprites::FXS_MIRA_BURST_0, 3, 2, 1, 1, 0, true, 160);
                    add_num_fx(o.x, o.y - 44, o.z, FX_DMG_NUM, (int32_t)fmaxf(1.0f, dealt * 0.45f), 24, 0);
                    break;
                }
            }
        }
    }

    if (e.hp <= 0.0f) {
        e.hp = 0.0f;
        e.alive = false;
        e.state = A_DOWN;
        e.timer = (e.type == E_BOSS) ? 95 : 58;

        double drop = (e.type == E_BOSS) ? (75.0 + wave_n * 18.0) : (9.0 + wave_n * 4.5);
        game::add_drops(drop, party_scavenger_pct());
        add_num_fx(e.x, e.y - 32, e.z, FX_DROP_TEXT, (int32_t)drop, 38);

        /* 撃破時回復チャーム (SOUL HARVEST) */
        if (game::st.mods.kill_heal_pct > 0) {
            for (auto& a : allies) {
                if (a.alive && a.state != A_DOWN) {
                    heal_ally_direct(a, a.hpmax * (game::st.mods.kill_heal_pct / 100.0f), false);
                }
            }
        }

        /* 嫌がらせ死亡時ギミック */
        if (e.type == E_SLIME && e.sub_kind == 0) {
            spawn_hazard(e.x, e.y, 0, (1.4f + wave_n * 0.35f) * (gp::g.chip_pct / 100.0f), 170);
        } else if (e.type == E_SLIME && e.sub_kind == 2 && wave_n >= 2) {
            spawn_hazard(e.x, e.y, 1, (2.2f + wave_n * 0.42f) * (gp::g.chip_pct / 100.0f), 170);
            float b_dmg = (5.0f + wave_n * 0.85f);
            for (auto& a : allies) {
                if (a.alive && a.state != A_DOWN && fabsf(a.x - e.x) < 46.0f && fabsf(a.y - e.y) < 26.0f) {
                    hurt_ally(a, b_dmg, (a.x >= e.x) ? 1 : -1, true, 2);
                }
            }
        }

        if (e.type == E_BOSS) {
            boss_down_x = e.x;
            boss_down_y = e.y;
            special_flash = 4;
            add_shake(5, 18);
            spawn_vfx_sprite(e.x, e.y - 36, e.z, vfx_sprites::FXS_EXPLODE_0, 6, 3, 1, 1, 0, true, 320);
        } else {
            spawn_vfx_sprite(e.x, e.y - 24, e.z, vfx_sprites::FXS_EXPLODE_0, 6, 3, 1, 1, 0, true, 224);
            spawn_crystal_gems(e.x, e.y, e.z, 3);
        }
    }
}

static void hurt_ally(Actor& a, float dmg, int8_t dir, bool is_chip, uint8_t ailment)
{
    if (!a.alive || a.invuln || a.state == A_DOWN || a.state == A_VICTORY || dir_mode != DIR_BATTLE) return;

    float chip_res = (game::st.mods.chip_res_pct + party_warden_pct()) / 100.0f;
    if (chip_res > 0.85f) chip_res = 0.85f;
    if (is_chip) dmg *= (1.0f - chip_res);
    if (a.type == game::STYLE_HEAVY_SLAM) dmg *= 0.82f;
    dmg -= (float)game::st.mods.flat_armor;
    if (dmg < 1.0f) dmg = 1.0f;

    /* 棘反射 */
    if (!is_chip && (game::st.mods.thorns > 0 || game::TALENTS[a.talent_id].trait == game::TRAIT_THORNS)) {
        float th = (float)game::st.mods.thorns + (game::TALENTS[a.talent_id].trait == game::TRAIT_THORNS ? 6.0f * a.rank : 0.0f);
        for (auto& e : enemies) {
            if (e.alive && fabsf(e.x - a.x) < 52.0f && fabsf(e.y - a.y) < 28.0f) {
                e.hp -= th; e.flash = 2;
                add_num_fx(e.x, e.y - 42, e.z, FX_DMG_NUM, (int32_t)th, 22);
                if (e.hp <= 0.0f) hit_enemy(e, 1.0f, (e.x >= a.x) ? 1 : -1, false, -1);
                break;
            }
        }
    }

    /* シールド吸収 */
    if (a.shield > 0.0f) {
        if (a.shield >= dmg) { a.shield -= dmg; dmg = 0.0f; }
        else { dmg -= a.shield; a.shield = 0.0f; }
    }

    if (ailment == 1 && a.poison_t < 150) a.poison_t = 150;
    if (ailment == 2 && a.burn_t < 120)   a.burn_t = 120;
    if (ailment == 3 && a.slow_t < 90)    a.slow_t = 90;

    if (dmg > 0.0f) {
        a.hp -= dmg;
        if (!is_chip) {
            a.flash = 3;
            a.state = A_HURT; a.timer = 8;
            a.vx = dir * 2.6f; a.vy = frand(-0.7f, 0.7f);
            a.hit_vib_t = 5; a.hit_vib_max = 5; a.hit_vib_amp = 4;
            set_squash(a, 3, 5);
            spawn_vfx_sprite(a.x, a.y - 26, a.z, vfx_sprites::FXS_HIT_LIGHT_0, 4, 2, dir, 1, 0, true);
        } else {
            a.flash = 1;
        }
        rage_add(a, is_chip ? 4.0f : 11.0f);
        add_num_fx(a.x + frand(-4, 4), a.y - 58, a.z, FX_DMG_NUM, -(int32_t)dmg, is_chip ? 24 : 30);
    }

    if (a.hp <= 0.0f) {
        a.hp = 0.0f;
        a.state = A_DOWN;
        a.timer = 150;
        add_shake(2, 8);
    }
}

void force_win()
{
    if (!wave_active) start_wave();
    if (dir_mode == DIR_BOSS_INTRO) {
        for (auto& e : enemies) if (e.alive && e.type == E_BOSS) { e.z = 0; e.vz = 0; }
        dir_mode = DIR_BATTLE; dir_timer = 0;
    }
    for (auto& e : enemies) {
        if (e.alive) {
            e.boss_phase_mask = 0x07;
            e.phase_armor_t = 0;
            hit_enemy(e, e.hp + 9999.0f, 1, true, 0, 4.5f);
        }
    }
    Serial.println("[cmd] forced WIN");
}

void force_gameover()
{
    wave_active = false;
    for (auto& e : enemies) e.alive = false;
    game::on_run_end(wave_n, false);
    flash_frames = 0; phase_banner_t = 0; cutin_draw = 0;
    dir_mode = DIR_GAME_OVER;
    dir_timer = 0; dir_total = 1;
    invalidate_all_bands();
    Serial.printf("[cmd] forced GAME OVER at Wave %u/50\r\n", (unsigned)wave_n);
}

void force_ending()
{
    wave_n = 50;
    game::st.wave_reached = 50;
    wave_active = false;
    for (auto& e : enemies) e.alive = false;
    for (auto& a : allies) if (a.alive) { a.state = A_VICTORY; a.dir = 1; }
    game::on_run_end(50, true);
    flash_frames = 0; phase_banner_t = 0; cutin_draw = 0;
    spawn_confetti_burst(cam_x + FB_W * 0.5f, BELT_Y0 + 50.0f, 100.0f, 24);
    dir_mode = DIR_ENDING;
    dir_timer = 0; dir_total = 1;
    invalidate_all_bands();
    Serial.println("[cmd] forced ENDING (Wave 50 Cleared!)");
}

/* ---------------- 必殺技演出 ---------------- */
static void spawn_crystal_rain(int dir)
{
    Fx* f = alloc_fx(); *f = Fx(); f->on = true;
    f->kind = FX_CRYSTAL_RAIN; f->sub = 0; f->layer = 1;
    f->x = cam_x + frand(28.0f, FB_W - 28.0f);
    f->y = frand(BELT_Y0 + 12.0f, BELT_Y1 - 12.0f);
    f->z = frand(140.0f, 200.0f);
    f->vx = dir * frand(2.2f, 4.0f); f->vz = -frand(10.5f, 14.0f);
    f->life = 28; f->max_life = 28;
}

static void spawn_aqua_particle(const Actor& a)
{
    Fx* f = alloc_fx(); *f = Fx(); f->on = true;
    f->kind = FX_AQUA_STORM; f->layer = 1;
    f->sub = (uint8_t)(rnd() % 3); f->v = (int32_t)(rnd() % 100);
    f->x = a.x + frand(-18.0f, 18.0f); f->y = a.y + frand(-20.0f, 20.0f); f->z = a.z + frand(6.0f, 34.0f);
    f->vx = a.dir * frand(6.0f, 9.8f); f->vy = frand(-0.6f, 0.6f); f->vz = frand(-0.4f, 0.8f);
    f->dir = a.dir; f->life = (int16_t)(18 + (rnd() % 12)); f->max_life = f->life;
}

static void spawn_slam_burst(const Actor& a)
{
    for (int i = 0; i < 4; i++) {
        int side = (i == 0) ? -1 : 1;
        spawn_vfx_sprite(a.x + side * (28.0f + i * 38.0f), a.y + frand(-14.0f, 14.0f), 0.0f,
                         vfx_sprites::FXS_SHOCKWAVE_0, 4, 4, side, 1, 0, false, 280);
    }
    spawn_vfx_sprite(a.x - 16, a.y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3, -1, 0, 1);
    spawn_vfx_sprite(a.x + 16, a.y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3,  1, 0, 1);
}

static void try_special(Actor& a, int ally_idx)
{
    if (a.rage < 100.0f || dir_mode != DIR_BATTLE) return;
    if (a.state != A_IDLE && a.state != A_WALK) return;

    const auto& td = game::TALENTS[a.talent_id];
    a.rage = 0.0f;
    a.state = A_SPECIAL;
    a.timer = 42;
    special_flash = 3;
    add_shake(2, 8);
    cutin_who_atlas = a.base_atlas;
    cutin_who_tid = a.talent_id;
    snprintf(cutin_who_name, sizeof(cutin_who_name), "%s", td.name);
    snprintf(cutin_name, sizeof(cutin_name), "%s", td.ult_name);
    cutin_draw = 48;

    /* PURE RIBBON チャーム効果: 必殺技発動で状態異常回復+小ヒール */
    if (game::st.mods.cleanse_ult > 0) {
        for (auto& al : allies) {
            if (!al.alive || al.state == A_DOWN) continue;
            al.poison_t = al.burn_t = al.slow_t = 0;
            heal_ally_direct(al, al.hpmax * (0.08f * game::st.mods.cleanse_ult));
        }
    }

    if (td.ult == game::ULT_CRYSTAL_RAIN) {
        a.z = 2.0f; a.vz = 4.6f; set_squash(a, 1, 6);
        for (int i = 0; i < 6; i++) spawn_crystal_rain(a.dir);
    } else if (td.ult == game::ULT_AQUA_STORM || td.ult == game::ULT_SANCTUARY) {
        a.z = 2.0f; a.vz = 3.2f; set_squash(a, 1, 6);
        for (int i = 0; i < 6; i++) spawn_aqua_particle(a);
    } else {
        a.z = 2.0f; a.vz = 6.2f; a.vx = a.dir * 2.5f; set_squash(a, 1, 6);
    }
    (void)ally_idx;
}

/* ---------------- 味方 AI (8戦闘スタイル・協働回避・状態異常進行) ---------------- */
static Actor* nearest_enemy(float x, float y)
{
    Actor* best = nullptr;
    float bd = 1e9f;
    for (auto& e : enemies) {
        if (!e.alive) continue;
        float d = fabsf(e.x - x) + fabsf(e.y - y) * 0.5f;
        if (d < bd) { bd = d; best = &e; }
    }
    return best;
}

static Actor* lowest_hp_ally()
{
    Actor* best = nullptr;
    float min_frac = 2.0f;
    for (auto& a : allies) {
        if (!a.alive || a.state == A_DOWN) continue;
        float f = a.hp / (a.hpmax > 1.0f ? a.hpmax : 1.0f);
        if (f < min_frac) { min_frac = f; best = &a; }
    }
    return best;
}

static Actor* pick_ally_target(const Actor& a)
{
    Actor* best = nullptr;
    float best_score = 1e9f;
    for (auto& e : enemies) {
        if (!e.alive) continue;
        float d = fabsf(e.x - a.x) + fabsf(e.y - a.y) * 0.6f;
        float score = d;
        if (a.type == game::STYLE_HEAVY_SLAM && (e.type == E_BOSS || e.type == E_GOLEM)) score -= 90.0f;
        if (a.type == game::STYLE_COMBO_BLADE && e.z > 8.0f) score -= 50.0f;
        if (score < best_score) { best_score = score; best = &e; }
    }
    return best;
}

static void start_ally_attack(Actor& a)
{
    a.active_combo = (a.type == game::STYLE_COMBO_BLADE || a.type == game::STYLE_PALADIN_WARD) ? (a.combo % 3) : 0;
    a.state = A_WINDUP;
    float spd_m = (game::st.mods.spd_pct / 100.0f) * (gp::g.move_pct / 100.0f);
    if (game::TALENTS[a.talent_id].trait == game::TRAIT_HASTE) spd_m *= 1.18f;
    if (a.haste_t > 0) spd_m *= 1.25f;
    if (a.slow_t > 0) spd_m *= 0.72f;
    int wf = (int)roundf((float)a.windup_f / fmaxf(0.65f, fminf(1.8f, spd_m)));
    if (wf < 2) wf = 2;
    a.timer = (uint16_t)wf;
    set_squash(a, 2, 3);
}

static void ally_step(Actor& a, int ally_idx)
{
    const auto& td = game::TALENTS[a.talent_id];
    float rm = 1.0f + 0.28f * (float)(a.rank > 0 ? a.rank - 1 : 0);
    float spd = (td.spd_x100 / 100.0f) * (game::st.mods.spd_pct / 100.0f) * (gp::g.move_pct / 100.0f);
    if (td.trait == game::TRAIT_HASTE) spd *= 1.18f;
    if (a.haste_t > 0) { a.haste_t--; spd *= 1.22f; }
    if (a.slow_t > 0)  { a.slow_t--; spd *= 0.68f; }
    if (a.ai_cd > 0) a.ai_cd--;

    constexpr float ALLY_MIN_X = 24.0f;
    constexpr float ALLY_MAX_X = STAGE_W - 24.0f;

    /* 状態異常・秒間リジェネ進行 */
    if (a.state != A_DOWN && dir_mode == DIR_BATTLE && wave_active) {
        if (++a.dot_cd >= 30) {
            a.dot_cd = 0;
            if (game::st.mods.regen_x10 > 0 && a.hp < a.hpmax) {
                heal_ally_direct(a, game::st.mods.regen_x10 / 20.0f, false);
            }
            if (a.poison_t > 0) {
                float p_dmg = (1.1f + wave_n * 0.28f) * (gp::g.chip_pct / 100.0f);
                hurt_ally(a, p_dmg, -a.dir, true, 0);
                a.poison_t = (a.poison_t > 30) ? (a.poison_t - 30) : 0;
            }
            if (a.burn_t > 0 && a.state != A_DOWN) {
                float b_dmg = (1.4f + wave_n * 0.34f) * (gp::g.chip_pct / 100.0f);
                hurt_ally(a, b_dmg, -a.dir, true, 0);
                a.burn_t = (a.burn_t > 30) ? (a.burn_t - 30) : 0;
            }
        }
    }

    if (a.state == A_VICTORY) {
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.80f);
        return;
    }

    if (a.state == A_DOWN) {
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.80f);
        if (a.timer > 0) --a.timer;
        if (a.timer == 0) {
            int standing = 0;
            for (int i = 0; i < ALLY_N; ++i)
                if (&allies[i] != &a && allies[i].alive && allies[i].state != A_DOWN) standing++;
            if (standing > 0) {
                a.hp = a.hpmax * 0.48f;
                a.poison_t = a.burn_t = a.slow_t = 0;
                a.state = A_IDLE;
                a.z = 1.0f; a.vz = 4.2f;
                a.invuln = true; a.invuln_t = 48;
                set_squash(a, 1, 6);
                spawn_vfx_sprite(a.x, a.y - 4, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, false, 180);
                flash_message("BACK IN!", 32);
            }
        }
        return;
    }

    if (a.invuln && --a.invuln_t == 0) a.invuln = false;
    if (a.state == A_HURT) {
        if (--a.timer == 0) a.state = A_IDLE;
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.84f);
        return;
    }

    /* 有効単発ダメージ算出 */
    float dmg_mult = (game::st.mods.dmg_pct / 100.0f) * (gp::g.dmg_pct / 100.0f) * (float)game::pet_mult();
    if (td.trait == game::TRAIT_BERSERK && a.hp < a.hpmax * 0.50f) dmg_mult *= 1.35f;
    if (game::st.mods.berserk_pct > 0 && a.hp < a.hpmax * 0.50f) dmg_mult *= (1.0f + game::st.mods.berserk_pct / 100.0f);
    float unit_dmg = td.dmg * rm * dmg_mult;

    /* A_SPECIAL 発動中 */
    if (a.state == A_SPECIAL) {
        float grav = (td.ult == game::ULT_GRAND_SLAM) ? 0.56f : 0.16f;
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.84f, grav);
        float ult_mult = (game::st.mods.ult_pct / 100.0f) * (gp::g.ult_pct / 100.0f) * (game::st.mods.star_core > 0 ? 1.35f : 1.0f);
        float base_dmg = unit_dmg * ult_mult;

        if (td.ult == game::ULT_CRYSTAL_RAIN) {
            if (a.timer % 2 == 0) { spawn_crystal_rain(a.dir); spawn_crystal_rain(a.dir); }
            if (a.timer == 36 || a.timer == 26 || a.timer == 16 || a.timer == 6) {
                if (a.timer == 16 || a.timer == 6) {
                    spawn_vfx_sprite(a.x + a.dir * 12, a.y - 24, a.z, vfx_sprites::FXS_LUMI_SMASH_0, 3, 3, a.dir, 1, 0, true);
                }
                for (auto& e : enemies) {
                    if (e.alive && fabsf(e.x - (cam_x + FB_W * 0.5f)) < FB_W * 0.58f) {
                        hit_enemy(e, base_dmg * 1.15f, (e.x >= a.x) ? 1 : -1, a.timer == 6, -1, 2.4f);
                    }
                }
                add_shake(1, 3);
            }
        } else if (td.ult == game::ULT_AQUA_STORM) {
            spawn_aqua_particle(a); spawn_aqua_particle(a);
            if (a.timer == 34 || a.timer == 24 || a.timer == 14 || a.timer == 4) {
                spawn_vfx_sprite(a.x + a.dir * 18, a.y - 26, a.z, vfx_sprites::FXS_MIRA_BURST_0, 3, 2, a.dir, 1, 0, true);
                for (auto& e : enemies) {
                    if (e.alive && fabsf(e.x - (cam_x + FB_W * 0.5f)) < FB_W * 0.62f) {
                        hit_enemy(e, base_dmg * 0.95f, a.dir, a.timer == 4, -1, 0.0f);
                    }
                }
                add_shake(1, 3);
            }
        } else if (td.ult == game::ULT_SANCTUARY) {
            if (a.timer == 26) {
                for (auto& al : allies) {
                    if (!al.alive) continue;
                    if (al.state == A_DOWN) { al.state = A_IDLE; al.hp = al.hpmax * 0.35f; }
                    else heal_ally_direct(al, al.hpmax * 0.32f * ult_mult, true);
                    al.poison_t = al.burn_t = al.slow_t = 0;
                    al.invuln = true; al.invuln_t = 45;
                    spawn_vfx_sprite(al.x, al.y - 6, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 180);
                    spawn_vfx_sprite(al.x, al.y - 42, 0.0f, vfx_sprites::FXS_HEART_BUFF, 1, 30, 1, 1, 4, true, 256);
                }
                for (auto& e : enemies) {
                    if (e.alive && fabsf(e.x - a.x) < 120.0f) hit_enemy(e, base_dmg * 1.4f, (e.x >= a.x) ? 1 : -1, true, -1, 3.2f);
                }
            }
        } else if (td.ult == game::ULT_STAR_BARRAGE) {
            if (a.timer % 4 == 0 && a.timer >= 6) {
                Actor* tgt = pick_ally_target(a);
                spawn_vfx_sprite(a.x + a.dir * 16, a.y - 24, a.z + 6.0f, vfx_sprites::FXS_MIRA_BURST_0, 3, 2, a.dir, 1, 0, true, 180);
                spawn_bolt(ally_idx, tgt, base_dmg * 0.85f, frand(-10.0f, 10.0f), 0);
            }
        } else if (td.ult == game::ULT_OVERDRIVE) {
            if (a.timer == 32 || a.timer == 20 || a.timer == 8) {
                spawn_vfx_sprite(a.x + a.dir * 12, a.y - 24, a.z, vfx_sprites::FXS_LUMI_SMASH_0, 3, 2, a.dir, 1, 0, true);
                for (auto& e : enemies) {
                    if (e.alive && fabsf(e.x - a.x) < 120.0f) hit_enemy(e, base_dmg * 1.55f, (e.x >= a.x) ? 1 : -1, a.timer == 8, -1, 3.0f);
                }
                if (a.timer == 8) {
                    for (auto& al : allies) {
                        if (al.alive && al.state != A_DOWN) {
                            rage_add(al, 32.0f);
                            al.haste_t = 180;
                        }
                    }
                }
            }
        } else { /* ULT_GRAND_SLAM */
            if (a.timer == 24) {
                a.z = 0.0f; a.vz = 0.0f; set_squash(a, 2, 7);
                spawn_slam_burst(a); add_shake(4, 16);
                for (auto& e : enemies) {
                    if (e.alive && fabsf(e.x - a.x) < 185.0f) hit_enemy(e, base_dmg * 4.2f, (e.x >= a.x) ? 1 : -1, true, -1, 6.8f);
                }
            }
        }
        if (--a.timer == 0) a.state = A_IDLE;
        return;
    }

    bool ranged_style = (a.type == game::STYLE_MAGIC_BOLT || a.type == game::STYLE_SPREAD_VOLLEY ||
                         a.type == game::STYLE_SNIPER_LANCE || a.type == game::STYLE_ALCH_MEND);

    if (a.state == A_WINDUP) {
        float grav = (a.z > 0.0f) ? 0.14f : 0.42f;
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.84f, grav);
        if (--a.timer == 0) {
            Actor* t = nearest_enemy(a.x, a.y);
            if (a.type == game::STYLE_COMBO_BLADE) {
                bool airborne = (a.z > 2.0f);
                uint8_t st = a.active_combo;
                bool aerial_finish = (airborne && a.air_combo >= 2);
                bool big_hit = (st == 2) || aerial_finish;
                float dmg = unit_dmg * (big_hit ? 1.75f : 1.0f);
                a.vx = a.dir * (big_hit ? 4.8f : 3.4f);
                spawn_vfx_sprite(a.x + a.dir * (big_hit || st == 1 ? 8 : 12), a.y - 24, a.z,
                                 big_hit ? vfx_sprites::FXS_LUMI_SMASH_0 : (st == 1 ? vfx_sprites::FXS_LUMI_SLASH_0 : vfx_sprites::FXS_LUMI_JAB_0),
                                 3, 2, a.dir, 1, 0, true);
                float lift = aerial_finish ? -5.2f : (airborne ? 2.8f : (st == 2 ? 5.6f : 0.0f));
                bool hit_any = false;
                for (auto& e : enemies) {
                    if (!e.alive) continue;
                    float dx = e.x - a.x;
                    if (dx * a.dir > -14 && fabsf(dx) < td.reach + 14 && fabsf(e.y - a.y) < 26 && fabsf(e.z - a.z) < 44.0f) {
                        hit_enemy(e, dmg, a.dir, big_hit, ally_idx, lift);
                        hit_any = true;
                    }
                }
                if (big_hit && hit_any) add_shake(aerial_finish ? 2 : 1, 3);
                a.combo = (st + 1) % 3;
                if (airborne && hit_any && !aerial_finish) { a.vz = 2.0f; a.air_combo++; }
                else if (airborne && aerial_finish) { a.vz = -2.8f; }
                else if (!airborne && st == 2 && hit_any) {
                    a.vz = 5.4f; a.z = 2.0f; a.vx = a.dir * 3.4f; a.air_combo = 0;
                    spawn_vfx_sprite(a.x, a.y - 2, 0.0f, vfx_sprites::FXS_DUST_0, 3, 2, -a.dir, 0, 1);
                }
            } else if (a.type == game::STYLE_FLURRY_DART) {
                a.vx = a.dir * 5.6f;
                spawn_vfx_sprite(a.x + a.dir * 10, a.y - 24, a.z, vfx_sprites::FXS_LUMI_SLASH_0, 3, 2, a.dir, 1, 0, true);
                int hits = 0;
                for (auto& e : enemies) {
                    if (!e.alive) continue;
                    float dx = e.x - a.x;
                    if (dx * a.dir > -16 && fabsf(dx) < td.reach + 22 && fabsf(e.y - a.y) < 28 && fabsf(e.z - a.z) < 44.0f) {
                        hit_enemy(e, unit_dmg * 1.10f, a.dir, (a.combo % 3 == 2), ally_idx, 1.8f);
                        if (++hits >= 2) break;
                    }
                }
                a.combo = (a.combo + 1) % 3;
            } else if (ranged_style && t) {
                spawn_vfx_sprite(a.x + a.dir * 16, a.y - 24, a.z + 6.0f, vfx_sprites::FXS_MIRA_BURST_0, 3, 2, a.dir, 1, 0, true, 192);
                if (a.type == game::STYLE_SPREAD_VOLLEY) {
                    spawn_bolt(ally_idx, t, unit_dmg * 0.68f, -10.0f, 0);
                    spawn_bolt(ally_idx, t, unit_dmg * 0.72f,   0.0f, 0);
                    spawn_bolt(ally_idx, t, unit_dmg * 0.68f,  10.0f, 0);
                } else if (a.type == game::STYLE_SNIPER_LANCE) {
                    spawn_bolt(ally_idx, t, unit_dmg * 1.38f, 0.0f, 1);
                } else if (a.type == game::STYLE_ALCH_MEND) {
                    spawn_bolt(ally_idx, t, unit_dmg * 0.92f, 0.0f, 0);
                    Actor* low = lowest_hp_ally();
                    if (low) {
                        heal_ally_direct(*low, (3.4f + 0.55f * a.rank) * (game::st.mods.hp_pct / 100.0f), true);
                        if (low->poison_t > 20) low->poison_t -= 20; else low->poison_t = 0;
                        if (low->burn_t > 20) low->burn_t -= 20; else low->burn_t = 0;
                    }
                } else { /* STYLE_MAGIC_BOLT */
                    if (a.combo == 2) {
                        spawn_bolt(ally_idx, t, unit_dmg * 0.85f, -6.0f, 0);
                        spawn_bolt(ally_idx, t, unit_dmg * 0.85f,  6.0f, 0);
                        a.vx = -a.dir * 3.4f;
                        if (a.z <= 0.0f) { a.z = 1.0f; a.vz = 3.8f; set_squash(a, 1, 5); }
                    } else {
                        spawn_bolt(ally_idx, t, unit_dmg, 0.0f, 0);
                    }
                }
                if (game::st.mods.twin_bolt > 0 && ((int)(rnd() % 100) < game::st.mods.twin_bolt)) {
                    spawn_bolt(ally_idx, t, unit_dmg * 0.65f, frand(-6, 6), 0);
                }
                a.combo = (a.combo + 1) % 3;
            } else { /* STYLE_HEAVY_SLAM or STYLE_PALADIN_WARD */
                a.vx = a.dir * 4.0f;
                bool jump_slam = (a.z > 2.0f);
                if (jump_slam) a.vz = -3.6f;
                spawn_vfx_sprite(a.x + a.dir * 12, a.y - 24, a.z, vfx_sprites::FXS_HEAVY_ARC_0, 3, 3, a.dir, 1, 0, true);
                spawn_vfx_sprite(a.x + a.dir * 26, a.y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, a.dir, 0, 0, false, 180);
                for (auto& e : enemies) {
                    if (!e.alive) continue;
                    float dx = e.x - a.x;
                    if (dx * a.dir > -16 && fabsf(dx) < td.reach + 28 && fabsf(e.y - a.y) < 30 && fabsf(e.z - a.z) < 42.0f) {
                        hit_enemy(e, unit_dmg, a.dir, true, ally_idx, jump_slam ? 4.8f : 0.0f);
                    }
                }
                if (a.type == game::STYLE_PALADIN_WARD && a.active_combo == 2) {
                    Actor* low = lowest_hp_ally();
                    if (low) {
                        heal_ally_direct(*low, low->hpmax * 0.06f, true);
                        low->poison_t = (low->poison_t > 36) ? (low->poison_t - 36) : 0;
                        low->burn_t = (low->burn_t > 36) ? (low->burn_t - 36) : 0;
                    }
                }
                a.combo = (a.combo + 1) % 3;
                add_shake(2, 5);
            }
            a.state = A_STRIKE;
            a.timer = td.strike;
            set_squash(a, 2, 3);
        }
        return;
    }

    if (a.state == A_STRIKE) {
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.82f, (a.z > 0.0f) ? 0.16f : 0.42f);
        if (--a.timer == 0) {
            if (a.type == game::STYLE_COMBO_BLADE && (a.combo != 0 || a.z > 4.0f)) {
                Actor* nt = nearest_enemy(a.x, a.y);
                if (nt && fabsf(nt->x - a.x) < td.reach + 32 && fabsf(nt->y - a.y) < 28) {
                    a.dir = (nt->x >= a.x) ? 1 : -1;
                    a.vx = a.dir * 2.8f; a.vy = (nt->y - a.y) * 0.14f;
                    start_ally_attack(a);
                    return;
                }
            }
            a.state = A_RECOVER;
            a.timer = (uint16_t)((ranged_style && a.combo != 0) ? (a.recover_f / 2) : a.recover_f);
        }
        return;
    }

    if (a.state == A_RECOVER) {
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.80f, (a.z > 0.0f) ? 0.24f : 0.42f);
        if (--a.timer == 0) a.state = A_IDLE;
        return;
    }

    /* IDLE / WALK */
    Actor* t = pick_ally_target(a);
    if (!t) {
        if (fabsf(a.home_x - a.x) > 100.0f) a.home_x = a.x;
        a.wander_t += 1;
        float tx = a.home_x + sinf(a.wander_t * 0.08f + a.home_x) * 22.0f;
        if (tx < ALLY_MIN_X) tx = ALLY_MIN_X; if (tx > ALLY_MAX_X) tx = ALLY_MAX_X;
        float dx = tx - a.x;
        if (fabsf(dx) > 3.0f) {
            a.vx += ((dx > 0 ? 1 : -1) * spd * 0.55f - a.vx) * 0.25f;
            a.vy *= 0.80f; a.dir = dx > 0 ? 1 : -1; a.state = A_WALK;
        } else {
            a.vx *= 0.75f; a.vy *= 0.75f; a.state = A_IDLE;
        }
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.96f);
        rage_add(a, 0.04f);
        return;
    }

    /* 危険床/敵大技予告からのスマート回避 */
    if (a.ai_cd == 0) {
        for (auto& e : enemies) {
            if (!e.alive || e.state != A_SPECIAL || e.timer > 18 || e.timer < 6) continue;
            if (fabsf(e.x - a.x) < 78.0f && fabsf(e.y - a.y) < 34.0f) {
                a.ai_cd = 50;
                int8_t away = (a.x >= e.x) ? 1 : -1;
                a.vx = away * spd * 2.2f;
                a.vy = (a.y > (BELT_Y0 + BELT_Y1) * 0.5f ? -1 : 1) * spd * 1.1f;
                if (a.z <= 0.0f) { a.z = 1.0f; a.vz = 4.2f; set_squash(a, 1, 5); }
                spawn_vfx_sprite(a.x, a.y - 2, 0.0f, vfx_sprites::FXS_DUST_0, 3, 2, -away, 0, 1);
                a.state = A_WALK;
                integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.96f);
                return;
            }
        }
    }

    float stand_off_y = (ally_idx == 0) ? -6.0f : ((ally_idx == 2) ? 10.0f : ((a.y < t->y) ? -22.0f : 22.0f));
    float ideal_x = ranged_style
        ? (t->x + (a.x < t->x ? -135.0f : 135.0f))
        : (t->x + (a.x < t->x ? -(float)td.reach * 0.72f : (float)td.reach * 0.72f));
    if (ideal_x < ALLY_MIN_X) ideal_x = ALLY_MIN_X; if (ideal_x > ALLY_MAX_X) ideal_x = ALLY_MAX_X;
    float ideal_y = t->y + stand_off_y;
    if (ideal_y < BELT_Y0 + 8) ideal_y = BELT_Y0 + 8; if (ideal_y > BELT_Y1 - 8) ideal_y = BELT_Y1 - 8;

    float dx_tgt = t->x - a.x;
    float adx = fabsf(dx_tgt), ady = fabsf(t->y - a.y);
    int8_t want_dir = (dx_tgt >= 0.0f) ? 1 : -1;
    if (want_dir != a.dir) {
        if (a.think_cd > 0) a.think_cd--;
        else { a.dir = want_dir; a.think_cd = (uint8_t)(4 + (rnd() % 5)); }
    }

    if (ranged_style && adx < 72.0f && ady < 32.0f) {
        a.dir = want_dir;
        a.vx += (-a.dir * spd * 1.9f - a.vx) * 0.35f;
        a.vy += ((a.y < (BELT_Y0 + BELT_Y1) * 0.5f ? 1.0f : -1.0f) * spd * 1.15f - a.vy) * 0.35f;
        if (a.z <= 0.0f && (rnd() % 100) < 22) {
            a.z = 1.0f; a.vz = 4.0f; set_squash(a, 1, 5);
            spawn_vfx_sprite(a.x, a.y - 2, 0.0f, vfx_sprites::FXS_DUST_0, 3, 2, a.dir, 0, 1);
        }
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.95f);
        a.state = A_WALK;
    } else if (!ranged_style && a.z <= 0.0f && adx > td.reach + 10.0f && adx < td.reach + 62.0f && ady < 24.0f && (rnd() % 100) < 24) {
        a.dir = want_dir;
        a.z = 2.0f; a.vz = 5.4f; a.vx = a.dir * spd * 2.0f; a.vy = (t->y - a.y) * 0.12f; a.air_combo = 0;
        set_squash(a, 1, 5);
        spawn_vfx_sprite(a.x - a.dir * 8, a.y - 2, 0.0f, vfx_sprites::FXS_DUST_0, 3, 2, -a.dir, 0, 1);
        start_ally_attack(a);
    } else if (adx > td.reach || (!ranged_style && ady > 18.0f)) {
        float mx = ideal_x - a.x, my = ideal_y - a.y;
        bool dashing = (fabsf(mx) > 48.0f);
        float dash_mult = dashing ? 1.80f : 1.0f;
        float tvx = (fabsf(mx) > 4.0f) ? ((mx > 0 ? 1 : -1) * spd * dash_mult) : 0.0f;
        float tvy = (fabsf(my) > 4.0f) ? ((my > 0 ? 1 : -1) * spd * (dashing ? 1.15f : 0.85f)) : 0.0f;
        a.vx += (tvx - a.vx) * 0.28f; a.vy += (tvy - a.vy) * 0.28f;
        integrate_vel(a, ALLY_MIN_X, ALLY_MAX_X, 0.96f);
        a.state = A_WALK;
        if (dashing && motion_tick % 7 == 0)
            spawn_vfx_sprite(a.x - a.dir * 8, a.y - 2, a.z, vfx_sprites::FXS_DUST_0, 3, 2, -a.dir, 0, 1, false, 192);
    } else {
        a.dir = want_dir;
        if (!ranged_style) {
            a.vx = a.dir * 2.4f;
            if (t->z > 14.0f && a.z <= 0.0f) { a.z = 2.0f; a.vz = 5.4f; a.air_combo = 0; set_squash(a, 1, 5); }
        }
        start_ally_attack(a);
    }
    try_special(a, ally_idx);
}

/* ---------------- 敵 AI & ボス 50% / 30% / 10% しきい値アクション ---------------- */
static Actor* pick_enemy_target(const Actor& e)
{
    /* Snipers & Blood Divers 優先でHP低い味方や後衛を狙い嫌がらせ */
    if ((e.type == E_BAT) && ((rnd() % 100) < 60)) {
        Actor* low = lowest_hp_ally();
        if (low) return low;
    }
    Actor* best = nullptr;
    float bd = 1e9f;
    for (auto& a : allies) {
        if (!a.alive || a.state == A_DOWN) continue;
        float d = fabsf(a.x - e.x) + fabsf(a.y - e.y) * 0.5f;
        if (d < bd) { bd = d; best = &a; }
    }
    return best;
}

static void spawn_telegraph_ring(float x, float y, float rx, float ry, int16_t frames)
{
    Fx* f = alloc_fx(); *f = Fx(); f->on = true;
    f->kind = FX_TELEGRAPH; f->x = x; f->y = y; f->z = 0.0f;
    f->vx = rx; f->vy = ry; f->life = frames; f->max_life = frames; f->layer = 0;
}

static void trigger_boss_phase(Actor& boss, int pct, uint8_t act_kind, const char* act_name)
{
    if (pct == 50) boss.boss_phase_mask |= 0x01;
    else if (pct == 30) boss.boss_phase_mask |= 0x02;
    else boss.boss_phase_mask |= 0x04;

    boss.phase_armor_t = 34;
    boss.invuln_t = 18;
    boss.state = A_SPECIAL;
    boss.timer = 44;
    special_flash = 3;
    add_shake(5, 14);

    snprintf(phase_banner_txt, sizeof(phase_banner_txt), "%d%% PHASE: %s!", pct, act_name);
    phase_banner_t = 90;
    spawn_vfx_sprite(boss.x, boss.y - 10, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 4, 1, 1, 0, false, 300);

    float hp_mult = 1.0f, dmg_mult = 1.0f;
    wave_scaling(wave_n, hp_mult, dmg_mult);
    (void)hp_mult;
    float p_guard = 1.0f - (game::st.mods.boss_guard_pct / 100.0f);
    if (p_guard < 0.4f) p_guard = 0.4f;
    float chip_s = (gp::g.chip_pct / 100.0f);
    float b_dmg = (5.2f + wave_n * 0.42f) * game::BOSSES[boss.talent_id].dmg_mult * dmg_mult * p_guard;

    switch (act_kind % 12) {
    case 0: /* THORN GARDEN */
    case 3: /* ACID FLOOD / MELTDOWN */
        for (int i = 0; i < 4; ++i) {
            spawn_hazard(cam_x + 70.0f + i * 110.0f, BELT_Y0 + 36.0f + (i % 2) * 90.0f, (act_kind == 3 ? 1 : 0), b_dmg * 0.42f * chip_s, 230);
        }
        break;
    case 1: /* BRIAR SWARM / BROOD CALL */
        spawn_enemy(E_SLIME, boss.x - 110.0f, boss.y - 28.0f, 0);
        spawn_enemy(E_BAT, boss.x + 110.0f, boss.y + 28.0f, (rnd() % 2));
        break;
    case 2: /* OVERGROWTH */
        spawn_hazard(boss.x - 70.0f, boss.y, 0, b_dmg * 0.45f * chip_s, 220);
        spawn_hazard(boss.x + 70.0f, boss.y, 0, b_dmg * 0.45f * chip_s, 220);
        for (int k = -2; k <= 2; ++k) {
            spawn_enemy_bolt(boss.x, boss.y - 24.0f, 12.0f, (boss.dir) * 5.0f, k * 1.1f, b_dmg * 0.72f, 0);
        }
        break;
    case 4: /* MAGMA MORTAR / CATACLYSM */
    case 9: /* VOID RIFT */
    case 11: /* LAST JUDGMENT */ {
        int count = (act_kind == 11) ? 5 : 3;
        for (int i = 0; i < count; ++i) {
            Actor* al = &allies[i % ALLY_N];
            float tx = al->alive ? al->x + frand(-28, 28) : (cam_x + frand(80, FB_W - 80));
            float ty = al->alive ? al->y + frand(-18, 18) : frand(BELT_Y0 + 20, BELT_Y1 - 20);
            spawn_telegraph_ring(tx, ty, 36.0f, 14.0f, 40);
            spawn_enemy_bolt(boss.x, boss.y - 30.0f, 22.0f, (tx - boss.x) / 42.0f, (ty - boss.y) / 42.0f, b_dmg * 0.85f, 1, tx, ty);
        }
        if (act_kind == 11) {
            for (int k = -2; k <= 2; ++k) spawn_enemy_bolt(boss.x, boss.y - 24.0f, 12.0f, boss.dir * 5.2f, k * 1.2f, b_dmg * 0.65f, 0);
        }
        break;
    }
    case 5: /* AEGIS QUAKES / GLACIAL GUARD */
        boss.guard_t = 135;
        spawn_telegraph_ring(boss.x, boss.y, 82.0f, 30.0f, 32);
        for (auto& a : allies) {
            if (a.alive && a.state != A_DOWN && fabsf(a.x - boss.x) < 120.0f) {
                hurt_ally(a, b_dmg * 0.85f, (a.x >= boss.x) ? 1 : -1, true, 3);
            }
        }
        break;
    case 6: /* SHADOW VOLLEY / ABYSS TEMPEST */
        boss.x += (boss.x < cam_x + FB_W * 0.5f) ? 110.0f : -110.0f;
        if (boss.x < 54.0f) boss.x = 54.0f; if (boss.x > STAGE_W - 54.0f) boss.x = STAGE_W - 54.0f;
        for (int k = -2; k <= 2; ++k) {
            spawn_enemy_bolt(boss.x, boss.y - 24.0f, 12.0f, -4.8f, k * 1.15f, b_dmg * 0.70f, 0);
            spawn_enemy_bolt(boss.x, boss.y - 24.0f, 12.0f,  4.8f, k * 1.15f, b_dmg * 0.70f, 0);
        }
        break;
    case 7: /* BLOOD ECLIPSE */
    case 10: /* ECLIPSE NOVA */
        for (auto& a : allies) {
            if (a.alive && a.state != A_DOWN) {
                hurt_ally(a, b_dmg * 0.75f, (a.x >= boss.x) ? 1 : -1, true, 1);
            }
        }
        boss.hp = fminf(boss.hpmax, boss.hp + boss.hpmax * 0.06f);
        spawn_enemy(E_BAT, boss.x + 80.0f, boss.y - 24.0f, 1);
        break;
    case 8: /* PERMAFROST / ABSOLUTE ZERO */
    default:
        for (int i = 0; i < 3; ++i) {
            spawn_hazard(cam_x + 90.0f + i * 140.0f, BELT_Y0 + 60.0f + (i % 2) * 70.0f, 2, b_dmg * 0.38f * chip_s, 230);
        }
        for (int k = -1; k <= 1; ++k) spawn_enemy_bolt(boss.x, boss.y - 22.0f, 10.0f, boss.dir * 5.0f, k * 1.2f, b_dmg * 0.70f, 2);
        break;
    }
}

void force_boss_phase(int pct)
{
    Actor* boss = nullptr;
    for (auto& e : enemies) if (e.alive && e.type == E_BOSS) { boss = &e; break; }
    if (!boss) {
        force_boss();
        for (auto& e : enemies) if (e.alive && e.type == E_BOSS) { boss = &e; break; }
        dir_mode = DIR_BATTLE; dir_timer = 0;
        if (boss) { boss->z = 0.0f; boss->vz = 0.0f; boss->state = A_WALK; }
    }
    if (!boss) return;
    const auto& bd = game::BOSSES[boss->talent_id];
    if (pct >= 45) {
        boss->hp = boss->hpmax * 0.49f;
        trigger_boss_phase(*boss, 50, bd.p50_kind, bd.p50_name);
    } else if (pct >= 20) {
        boss->boss_phase_mask |= 0x01;
        boss->hp = boss->hpmax * 0.29f;
        trigger_boss_phase(*boss, 30, bd.p30_kind, bd.p30_name);
    } else {
        boss->boss_phase_mask |= 0x03;
        boss->hp = boss->hpmax * 0.09f;
        trigger_boss_phase(*boss, 10, bd.p10_kind, bd.p10_name);
    }
    Serial.printf("[cmd] forced boss phase %d%%\r\n", pct);
}

static void enemy_step(Actor& e)
{
    constexpr float ENEMY_MIN_X = 20.0f;
    constexpr float ENEMY_MAX_X = STAGE_W - 20.0f;
    if (e.ai_cd > 0) e.ai_cd--;
    if (e.guard_t > 0) e.guard_t--;
    if (e.phase_armor_t > 0) e.phase_armor_t--;
    if (e.slow_t > 0) e.slow_t--;
    if (e.spawn_t) { e.spawn_t--; return; }

    /* ボス 50% / 30% / 10% しきい値判定 */
    if (e.type == E_BOSS && e.hpmax > 0.0f) {
        const auto& bd = game::BOSSES[e.talent_id];
        if (!(e.boss_phase_mask & 0x01) && e.hp <= e.hpmax * 0.50f) {
            trigger_boss_phase(e, 50, bd.p50_kind, bd.p50_name);
            return;
        } else if ((e.boss_phase_mask & 0x01) && !(e.boss_phase_mask & 0x02) && e.hp <= e.hpmax * 0.30f) {
            trigger_boss_phase(e, 30, bd.p30_kind, bd.p30_name);
            return;
        } else if ((e.boss_phase_mask & 0x02) && !(e.boss_phase_mask & 0x04) && e.hp <= e.hpmax * 0.10f) {
            trigger_boss_phase(e, 10, bd.p10_kind, bd.p10_name);
            return;
        }
    }

    if (e.type == E_BAT && e.state == A_WALK) {
        e.vz *= 0.80f;
        float hover_z = 14.0f + sinf((motion_tick + e.home_x) * 0.16f) * 5.0f;
        e.z += (hover_z - e.z) * 0.2f;
    }

    if (e.state == A_HURT) {
        if (--e.timer == 0) e.state = A_WALK;
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.84f, enemy_gravity(e));
        return;
    }

    float hp_mult = 1.0f, dmg_mult = 1.0f;
    wave_scaling(wave_n, hp_mult, dmg_mult);
    (void)hp_mult;

    if (e.state == A_SPECIAL) {
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.84f, enemy_gravity(e));
        e.timer--;
        if (e.timer == 14) {
            if (e.type == E_BOSS) {
                e.z = 0.0f; e.vz = 0.0f; set_squash(e, 2, 8); add_shake(5, 14);
                spawn_vfx_sprite(e.x + e.dir * 26, e.y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 4, e.dir, 1, 0, false, 300);
                float dmg = 13.5f * game::BOSSES[e.talent_id].dmg_mult * dmg_mult;
                for (auto& a : allies) {
                    if (a.alive && a.state != A_DOWN && fabsf(a.x - e.x) < 76.0f && fabsf(a.y - e.y) < 34.0f && a.z < 18.0f) {
                        hurt_ally(a, dmg, (a.x >= e.x) ? 1 : -1);
                    }
                }
            } else if (e.type == E_GOLEM) {
                set_squash(e, 2, 7); add_shake(3, 10);
                spawn_vfx_sprite(e.x + e.dir * 22, e.y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 4, e.dir, 1, 0, false, 230);
                float dmg = 9.5f * dmg_mult;
                for (auto& a : allies) {
                    if (a.alive && a.state != A_DOWN && fabsf(a.x - e.x) < 58.0f && fabsf(a.y - e.y) < 28.0f && a.z < 16.0f) {
                        hurt_ally(a, dmg, (a.x >= e.x) ? 1 : -1);
                    }
                }
            }
        }
        if (e.timer == 0) {
            e.state = A_RECOVER;
            e.timer = (e.type == E_BOSS && e.hp < e.hpmax * 0.5f) ? 14 : 20;
        }
        return;
    }

    if (e.state == A_WINDUP) {
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.82f, enemy_gravity(e));
        if (--e.timer == 0) {
            float reach = (e.type == E_GOLEM || e.type == E_BOSS) ? 48.0f : 32.0f;
            float intro_soft = (wave_n == 1) ? 0.65f : ((wave_n == 2) ? 0.82f : 1.0f);
            float dmg = ((e.type == E_BOSS) ? 11.2f * game::BOSSES[e.talent_id].dmg_mult : (e.type == E_GOLEM ? 7.8f : (e.type == E_BAT ? 4.8f : 5.2f))) * dmg_mult * intro_soft;

            if (e.type == E_BOSS) {
                e.vx = e.dir * 3.2f;
                spawn_vfx_sprite(e.x + e.dir * 18, e.y - 28, 0.0f, vfx_sprites::FXS_BOSS_LASH_0, 3, 3, e.dir, 1, 0, true);
                for (auto& a : allies) {
                    if (!a.alive || a.state == A_DOWN) continue;
                    float dx = a.x - e.x, dy = a.y - e.y;
                    if (dx * e.dir > -16 && fabsf(dx) < reach + 24 && fabsf(dy) < 28) hurt_ally(a, dmg, dx >= 0 ? 1 : -1);
                }
                add_shake(2, 5);
            } else if (e.type == E_GOLEM) {
                if (e.sub_kind == 0) { /* Magma Mortar: 迫撃砲弾発射 */
                    Actor* t = pick_enemy_target(e);
                    if (t) {
                        float tx = t->x + frand(-14, 14), ty = t->y + frand(-10, 10);
                        spawn_telegraph_ring(tx, ty, 32.0f, 12.0f, 40);
                        spawn_enemy_bolt(e.x, e.y - 28.0f, 20.0f, (tx - e.x) / 40.0f, (ty - e.y) / 40.0f, dmg * 0.85f, 1, tx, ty);
                    }
                    e.ai_cd = 75;
                } else { /* Thorn Guard */
                    e.vx = e.dir * 2.8f;
                    spawn_vfx_sprite(e.x + e.dir * 14, e.y - 24, 0.0f, vfx_sprites::FXS_HEAVY_ARC_0, 3, 3, e.dir, 1, 0, true, 224);
                    Actor* t = pick_enemy_target(e);
                    if (t && fabsf(t->x - e.x) < reach + 8 && fabsf(t->y - e.y) < 22) hurt_ally(*t, dmg, (t->x > e.x) ? 1 : -1);
                    if (rnd() % 100 < 40) e.guard_t = 90;
                }
            } else if (e.type == E_BAT) {
                Actor* t = pick_enemy_target(e);
                if (e.sub_kind == 0 && t) { /* Curse Sniper */
                    float dist = hypotf(t->x - e.x, t->y - e.y);
                    float spd_b = 5.4f;
                    float vx = (t->x - e.x) / fmaxf(20.0f, dist) * spd_b;
                    float vy = (t->y - e.y) / fmaxf(20.0f, dist) * spd_b;
                    spawn_enemy_bolt(e.x + e.dir * 10, e.y - 18.0f, e.z, vx, vy, dmg * 0.88f, 0);
                    e.ai_phase = 40;
                } else { /* Blood Diver */
                    e.vx = e.dir * 4.8f;
                    spawn_vfx_sprite(e.x + e.dir * 10, e.y - 20, e.z, vfx_sprites::FXS_BOSS_LASH_0, 3, 2, e.dir, 1, 0, true, 140);
                    if (t && fabsf(t->x - e.x) < reach + 8 && fabsf(t->y - e.y) < 20) {
                        hurt_ally(*t, dmg, (t->x > e.x) ? 1 : -1);
                        e.hp = fminf(e.hpmax, e.hp + e.hpmax * 0.08f);
                    }
                    e.ai_phase = 34;
                }
            } else { /* E_SLIME */
                e.vx = e.dir * 3.8f;
                spawn_vfx_sprite(e.x + e.dir * 8, e.y - 14, 0.0f, vfx_sprites::FXS_DUST_0, 3, 2, -e.dir, 0, 1);
                Actor* t = pick_enemy_target(e);
                if (t && fabsf(t->x - e.x) < reach + 6 && fabsf(t->y - e.y) < 20) {
                    hurt_ally(*t, dmg, (t->x > e.x) ? 1 : -1, false, (e.sub_kind == 0 ? 1 : (e.sub_kind == 1 ? 3 : 2)));
                    if (e.sub_kind == 1) t->rage = fmaxf(0.0f, t->rage - 14.0f);
                }
                if (e.sub_kind == 0 && (rnd() % 100) < (wave_n <= 2 ? 24 : 42)) {
                    spawn_hazard(e.x, e.y, 0, (1.2f + wave_n * 0.30f) * (gp::g.chip_pct / 100.0f), 150);
                }
            }
            e.state = A_STRIKE;
            e.timer = (e.type == E_BOSS || e.type == E_GOLEM) ? 7 : 5;
            set_squash(e, 2, 4);
        }
        return;
    }

    if (e.state == A_STRIKE) {
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.82f, enemy_gravity(e));
        if (--e.timer == 0) {
            e.state = A_RECOVER;
            bool angry_boss = (e.type == E_BOSS && e.hp < e.hpmax * 0.5f);
            e.timer = angry_boss ? 11 : ((e.type == E_BOSS) ? 15 : 18);
        }
        return;
    }

    if (e.state == A_RECOVER) {
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.82f, enemy_gravity(e));
        if (--e.timer == 0) e.state = A_WALK;
        return;
    }

    /* WALK */
    Actor* t = pick_enemy_target(e);
    if (!t) {
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.85f, enemy_gravity(e));
        e.state = A_IDLE;
        return;
    }
    float dx = t->x - e.x, dy = t->y - e.y;

    if (e.type == E_BAT && e.ai_phase > 0) {
        e.ai_phase--;
        int8_t away = (dx >= 0) ? -1 : 1;
        e.vx += (away * 1.9f - e.vx) * 0.25f;
        e.vy += ((e.y < (BELT_Y0 + BELT_Y1) * 0.5f ? 1.0f : -1.0f) * 1.2f - e.vy) * 0.25f;
        e.dir = -away;
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.96f, enemy_gravity(e));
        return;
    }

    float reach = (e.type == E_GOLEM || e.type == E_BOSS) ? 40.0f : 28.0f;
    if (e.type == E_BAT && e.sub_kind == 0) reach = 148.0f;
    if (e.type == E_GOLEM && e.sub_kind == 0 && e.ai_cd == 0) reach = 160.0f;

    float sp = (e.type == E_BAT) ? 1.75f : (e.type == E_GOLEM ? 0.72f : (e.type == E_BOSS ? 0.86f : 1.05f));
    if (e.type == E_BOSS && e.hp < e.hpmax * 0.5f) sp = 1.12f;
    if (e.slow_t > 0) sp *= 0.65f;

    if (fabsf(dx) > reach || (reach < 80.0f && fabsf(dy) > 18.0f)) {
        if (e.type == E_SLIME) {
            uint8_t hop = (motion_tick + (uint8_t)e.x) % 24;
            if (hop == 4 && e.z <= 0.0f) { e.z = 1.0f; e.vz = 2.4f; set_squash(e, 1, 3); }
        }
        e.dir = dx >= 0 ? 1 : -1;
        float tvx = (fabsf(dx) > reach * 0.7f) ? (e.dir * sp) : 0.0f;
        float tvy = (fabsf(dy) > 4.0f) ? ((dy > 0 ? 1 : -1) * sp * 0.75f) : 0.0f;
        e.vx += (tvx - e.vx) * 0.25f; e.vy += (tvy - e.vy) * 0.25f;
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.96f, enemy_gravity(e));
    } else {
        integrate_vel(e, ENEMY_MIN_X, ENEMY_MAX_X, 0.82f, enemy_gravity(e));
        e.dir = dx >= 0 ? 1 : -1;
        if ((e.type == E_BOSS || (e.type == E_GOLEM && e.sub_kind == 1)) && e.ai_cd == 0 && ((rnd() % 100) < (e.type == E_BOSS ? 45 : 35))) {
            e.state = A_SPECIAL;
            e.timer = 44;
            e.ai_cd = (e.type == E_BOSS && e.hp < e.hpmax * 0.5f) ? 75 : 110;
            if (e.type == E_BOSS) { e.z = 1.5f; e.vz = 3.2f; }
            set_squash(e, 1, 6);
            spawn_telegraph_ring(e.x, e.y, e.type == E_BOSS ? 74.0f : 56.0f, e.type == E_BOSS ? 26.0f : 20.0f, 30);
        } else {
            e.state = A_WINDUP;
            e.timer = e.windup_f;
            set_squash(e, 2, 4);
        }
    }
}

/* ---------------- 弾・床ハザード・FX 進行 ---------------- */
static void fx_step()
{
    for (auto& f : fxs) {
        if (!f.on) continue;
        if (f.kind == FX_DMG_NUM || f.kind == FX_DROP_TEXT || f.kind == FX_HEAL_NUM) {
            f.y -= (f.life > f.max_life - 6) ? 1.3f : 0.55f;
        } else if (f.kind == FX_BOLT) {
            if (f.sub == 1) {
                /* 貫通狙撃ランス (1敵につき1回命中ビットマスク管理) */
                f.x += f.vx; f.y += f.vy * 0.2f;
                for (int ei = 0; ei < ENEMY_N; ++ei) {
                    Actor& e = enemies[ei];
                    if (e.alive && !(f.v & (1 << ei)) && fabsf(e.x - f.x) < 20.0f && fabsf(e.y - 20.0f - f.y) < 28.0f) {
                        f.v |= (1 << ei);
                        spawn_vfx_sprite(f.x, f.y, f.z, vfx_sprites::FXS_MIRA_BURST_0, 3, 2, f.dir, 1, 0, true, 192);
                        hit_enemy(e, f.dmg, f.dir, true, f.src);
                    }
                }
            } else {
                Actor& t = enemies[f.v];
                if (t.alive) {
                    float ty = t.y - 22.0f;
                    f.vy += (ty - f.y) * 0.10f;
                    if (f.vy > 3.6f) f.vy = 3.6f; if (f.vy < -3.6f) f.vy = -3.6f;
                    f.vz += (t.z - f.z) * 0.14f;
                }
                f.x += f.vx;
                f.y += f.vy + sinf(f.life * 0.65f) * 2.2f;
                f.z += f.vz;
                if (f.z < 0.0f) f.z = 0.0f;
                if (!t.alive || f.life <= 1 || (t.x - f.x) * f.vx <= 0) {
                    if (t.alive) {
                        spawn_vfx_sprite(f.x, f.y, f.z, vfx_sprites::FXS_MIRA_BURST_0, 3, 2, f.dir, 1, 0, true, 200);
                        hit_enemy(t, f.dmg, f.vx > 0 ? 1 : -1, false, f.src);
                    }
                    f.on = false;
                    continue;
                }
            }
        } else if (f.kind == FX_EBOLT) {
            f.x += f.vx;
            f.y += f.vy;
            if (f.sub == 1) { /* Arcing Magma Mortar */
                f.z += f.vz; f.vz -= 0.23f;
                if (f.z <= 0.0f || f.life <= 1) {
                    spawn_vfx_sprite(f.x, f.y, 0.0f, vfx_sprites::FXS_EXPLODE_0, 6, 2, 1, 1, 0, true, 192);
                    spawn_hazard(f.x, f.y, 1, f.dmg * 0.42f * (gp::g.chip_pct / 100.0f), 170);
                    for (auto& a : allies) {
                        if (a.alive && a.state != A_DOWN && fabsf(a.x - f.x) < 38.0f && fabsf(a.y - f.y) < 22.0f) {
                            hurt_ally(a, f.dmg, (a.x >= f.x) ? 1 : -1, true, 2);
                        }
                    }
                    f.on = false;
                    continue;
                }
            } else { /* Curse/Frost Needle */
                for (auto& a : allies) {
                    if (a.alive && a.state != A_DOWN && fabsf(a.x - f.x) < 18.0f && fabsf(a.y - 20.0f - f.y) < 22.0f) {
                        hurt_ally(a, f.dmg, f.dir, true, (f.sub == 2 || (rnd() % 100) < 60) ? 3 : 1);
                        f.on = false;
                        break;
                    }
                }
            }
        } else if (f.kind == FX_HAZARD) {
            if (dir_mode == DIR_BATTLE && wave_active && (f.life % 32 == 0)) {
                for (auto& a : allies) {
                    if (a.alive && a.state != A_DOWN && a.z < 10.0f &&
                        fabsf(a.x - f.x) < f.vx && fabsf(a.y - f.y) < f.vy) {
                        uint8_t ail = (f.sub == 0) ? 1 : (f.sub == 1 ? 2 : 3);
                        hurt_ally(a, f.dmg, (a.x >= f.x) ? 1 : -1, true, ail);
                    }
                }
            }
        } else if (f.kind == FX_CRYSTAL_RAIN) {
            if (f.sub == 0) {
                f.x += f.vx; f.y += f.vy; f.z += f.vz;
                if (f.z <= 0.0f) {
                    f.z = 0.0f;
                    spawn_vfx_sprite(f.x, f.y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, false, 150);
                    f.on = false;
                    continue;
                }
            }
        } else if (f.kind == FX_AQUA_STORM) {
            f.x += f.vx;
            f.y += sinf(f.life * 0.45f + (float)f.v) * 2.8f + f.vy;
            f.z += cosf(f.life * 0.45f + (float)f.v) * 1.8f + f.vz;
            if (f.z < 0.0f) f.z = 0.0f;
        } else if (f.kind == FX_SPR) {
            if (f.phys_mode == 1) {
                f.x += f.dir * -0.5f; f.z += 0.25f;
            } else if (f.phys_mode == 2) {
                if (f.life > 32) {
                    f.x += f.vx; f.y += f.vy; f.z += f.vz;
                    f.vx *= 0.94f; f.vy *= 0.94f; f.vz -= 0.44f;
                    if (f.z <= 0.0f) { f.z = 0.0f; f.vz = fabsf(f.vz) * 0.48f; }
                } else {
                    float target_wx = cam_x + 210.0f;
                    f.x += (target_wx - f.x) * 0.16f;
                    f.y += (8.0f - f.y) * 0.16f;
                    f.z *= 0.80f;
                }
            } else if (f.phys_mode == 3) {
                f.x += f.vx + sinf(f.life * 0.22f + (float)f.v) * 0.7f;
                f.y += f.vy; f.z += f.vz;
                f.vx *= 0.93f; f.vy *= 0.93f;
                f.vz -= 0.24f; if (f.vz < -1.1f) f.vz = -1.1f;
                if (f.z <= 0.0f) { f.z = 0.0f; f.vz = 0.0f; }
            } else if (f.phys_mode == 4) {
                f.z += 0.7f;
            }
        }
        if (--f.life <= 0) f.on = false;
    }
}

/* ---------------- ゲームステップ ---------------- */
void step()
{
    frame++;
    if (special_flash > 0) special_flash--;
    if (shake > 0) shake--;
    if (flash_frames > 0) flash_frames--;
    if (cutin_draw > 0) cutin_draw--;
    if (phase_banner_t > 0) phase_banner_t--;

    motion_tick++;
    for (auto& a : allies) { if (a.flash) --a.flash; if (a.hit_vib_t) --a.hit_vib_t; if (a.squash_t) --a.squash_t; }
    for (auto& e : enemies) {
        if (e.flash) --e.flash;
        if (e.hit_vib_t) --e.hit_vib_t;
        if (e.squash_t) --e.squash_t;
        if (!e.alive && e.state == A_DOWN && e.timer) --e.timer;
    }

    if (dir_mode == DIR_TITLE) {
        if (dir_timer < 60000) dir_timer++;
        cam_x += 0.55f;
        if (cam_x > STAGE_W - FB_W - 60.0f) cam_x = 60.0f;
        int n_alive = 0;
        for (int i = 0; i < ALLY_N; ++i) if (allies[i].alive) n_alive++;
        int idx = 0;
        for (int i = 0; i < ALLY_N; ++i) {
            Actor& a = allies[i];
            if (!a.alive) continue;
            float offset_x = (n_alive == 1) ? 0.0f : (idx - (n_alive - 1) * 0.5f) * 58.0f;
            a.x = cam_x + FB_W * 0.5f + offset_x;
            a.y = 228.0f + ((idx == 1) ? 14.0f : (idx == 2 ? -12.0f : 0.0f));
            a.dir = 1;
            a.state = A_WALK;
            if (a.z > 0.0f || a.vz != 0.0f) {
                a.z += a.vz; a.vz -= 0.42f;
                if (a.z <= 0.0f) { a.z = 0.0f; a.vz = 0.0f; set_squash(a, 2, 5); }
            } else if (((dir_timer + idx * 45) % 150) == 60) {
                a.z = 0.5f; a.vz = 3.2f; set_squash(a, 1, 5);
            }
            idx++;
        }
        if ((dir_timer % 28) == 0) {
            spawn_vfx_sprite(cam_x + frand(48, FB_W - 48), frand(135, 250), frand(8, 36),
                             vfx_sprites::FXS_MIRA_BURST_0, 3, 4, 1, 1, 4, true, 200);
        }
        fx_step();
        return;
    }

    if (dir_mode == DIR_OPENING) {
        uint16_t f = dir_timer++;
        Actor& lumi = allies[0];

        if (f < 65) {
            /* Scene 1: 平和な水晶庭園を歩くルミ */
            cam_x += 0.65f;
            if (lumi.alive) {
                lumi.x = cam_x + 175.0f;
                lumi.y = 228.0f;
                lumi.dir = 1;
                lumi.state = A_WALK;
                if (f == 28) { lumi.z = 0.5f; lumi.vz = 3.2f; set_squash(lumi, 1, 5); }
                if (lumi.z > 0.0f || lumi.vz != 0.0f) {
                    lumi.z += lumi.vz; lumi.vz -= 0.42f;
                    if (lumi.z <= 0.0f) { lumi.z = 0.0f; lumi.vz = 0.0f; set_squash(lumi, 2, 5); }
                }
            }
            if ((f % 18) == 0) {
                spawn_vfx_sprite(cam_x + frand(60, FB_W - 60), frand(120, 240), frand(8, 36),
                                 vfx_sprites::FXS_MIRA_BURST_0, 3, 4, 1, 1, 4, true, 200);
            }
        } else if (f < 140) {
            /* Scene 2: 異変・魔王軍襲来！ */
            if (f == 65) {
                special_flash = 4;
                add_shake(5, 14);
                Actor* b = spawn_enemy(E_BOSS, cam_x + 360.0f, 226.0f);
                if (b) {
                    b->base_atlas = 3; b->use_orig_pal = true;
                    b->dir = -1; b->z = 210.0f; b->vz = -16.0f; b->state = A_IDLE; b->spawn_t = 0;
                }
                Actor* s1 = spawn_enemy(E_SLIME, cam_x + 445.0f, 192.0f, 0);
                if (s1) { s1->dir = -1; s1->spawn_t = 0; }
                Actor* b1 = spawn_enemy(E_BAT, cam_x + 460.0f, 252.0f, 0);
                if (b1) { b1->dir = -1; b1->spawn_t = 0; }
                if (lumi.alive) {
                    lumi.state = A_IDLE; lumi.dir = 1;
                    lumi.z = 1.0f; lumi.vz = 3.4f; set_squash(lumi, 1, 6);
                }
            }
            if (lumi.alive) {
                lumi.x += (cam_x + 128.0f - lumi.x) * 0.14f;
                lumi.dir = 1;
                if (lumi.z > 0.0f || lumi.vz != 0.0f) {
                    lumi.z += lumi.vz; lumi.vz -= 0.45f;
                    if (lumi.z <= 0.0f) { lumi.z = 0.0f; lumi.vz = 0.0f; set_squash(lumi, 2, 6); }
                }
            }
            for (auto& e : enemies) {
                if (!e.alive) continue;
                e.dir = -1;
                if (e.type == E_BOSS) {
                    if (e.z > 0.0f) {
                        e.z += e.vz; e.vz -= 0.8f;
                        if (e.z <= 0.0f) {
                            e.z = 0.0f; e.vz = 0.0f; set_squash(e, 2, 9);
                            spawn_vfx_sprite(e.x, e.y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 4, 1, 1, 0, true, 300);
                            spawn_vfx_sprite(e.x - 28, e.y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3, -1, 0, 1);
                            spawn_vfx_sprite(e.x + 28, e.y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3,  1, 0, 1);
                            add_shake(7, 16);
                        }
                    }
                } else {
                    float target_x = (e.type == E_SLIME) ? (cam_x + 312.0f) : (cam_x + 340.0f);
                    if (e.x > target_x) e.x -= 2.2f;
                }
            }
        } else if (f < 215) {
            /* Scene 3: ルミ覚醒＆魔王撤退 */
            if (f == 140) {
                special_flash = 6;
                add_shake(4, 10);
                if (lumi.alive) {
                    lumi.state = A_SPECIAL; lumi.dir = 1;
                    set_squash(lumi, 1, 6);
                    spawn_vfx_sprite(lumi.x, lumi.y - 6, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 240);
                    spawn_vfx_sprite(lumi.x, lumi.y - 28, 0.0f, vfx_sprites::FXS_MIRA_BURST_0, 3, 3, 1, 1, 0, true, 256);
                }
            }
            if (f == 175) {
                for (auto& e : enemies) {
                    if (e.alive && e.type == E_BOSS) {
                        e.vz = 10.5f; e.z = 1.0f;
                        spawn_vfx_sprite(e.x, e.y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 240);
                        spawn_vfx_sprite(e.x, e.y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3, 1, 0, 1);
                    }
                }
            }
            if (f >= 175) {
                for (auto& e : enemies) {
                    if (!e.alive) continue;
                    if (e.type == E_BOSS) {
                        e.z += e.vz; e.vz += 0.65f;
                        if (e.z > 240.0f) e.alive = false;
                    } else {
                        e.x += 1.6f;
                    }
                }
            }
            if (f == 192 && lumi.alive) lumi.state = A_WALK;
        } else {
            /* Scene 4: 戦場へ突入！ WAVE 1 BEGIN! */
            float target_cam = STAGE_W * 0.5f - FB_W * 0.5f;
            cam_x += (target_cam - cam_x) * 0.14f;
            if (lumi.alive) {
                float target_x = target_cam + FB_W * 0.5f - 20.0f;
                lumi.x += (target_x - lumi.x) * 0.14f;
                lumi.dir = 1;
                lumi.state = A_WALK;
            }
            for (auto& e : enemies) if (e.alive) e.x += 5.0f;
            if (f >= OP_DURATION - 1) {
                finish_opening_to_wave1();
                return;
            }
        }

        fx_step();
        return;
    }

    if (dir_mode == DIR_GAME_OVER || dir_mode == DIR_ENDING) {
        if (dir_timer < 60000) dir_timer++;
        if (dir_mode == DIR_ENDING && (dir_timer % 18 == 0)) {
            spawn_confetti_burst(cam_x + frand(40, FB_W - 40), BELT_Y0 + 30.0f, 150.0f, 3);
        }
        fx_step();
        return;
    }

    if (dir_mode == DIR_BOSS_INTRO) {
        uint16_t elapsed = dir_total - dir_timer;
        Actor* boss = nullptr;
        for (auto& e : enemies) if (e.alive && e.type == E_BOSS) { boss = &e; break; }
        if (boss) {
            if (elapsed == 18) { boss->z = 190.0f; boss->vz = -16.0f; }
            if (elapsed >= 18 && boss->z > 0.0f) {
                boss->z += boss->vz; boss->vz -= 0.7f;
                if (boss->z <= 0.0f) {
                    boss->z = 0.0f; boss->vz = 0.0f; set_squash(*boss, 2, 8);
                    spawn_vfx_sprite(boss->x, boss->y, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 4, 1, 1, 0, false, 300);
                    spawn_vfx_sprite(boss->x - 24, boss->y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3, -1, 0, 1);
                    spawn_vfx_sprite(boss->x + 24, boss->y + 4, 0.0f, vfx_sprites::FXS_DUST_0, 3, 3,  1, 0, 1);
                    add_shake(6, 16);
                }
            }
            float target_cam = (allies[0].x * 0.45f + boss->x * 0.55f) - FB_W * 0.5f;
            if (target_cam < 0.0f) target_cam = 0.0f; if (target_cam > STAGE_W - FB_W) target_cam = STAGE_W - FB_W;
            cam_x += (target_cam - cam_x) * 0.18f;
            for (auto& a : allies) if (a.alive && a.state != A_DOWN) { a.dir = (boss->x >= a.x) ? 1 : -1; a.state = A_IDLE; }
        }
        fx_step();
        if (--dir_timer == 0) {
            if (boss) { boss->z = 0.0f; boss->vz = 0.0f; boss->state = A_WALK; }
            dir_mode = DIR_BATTLE;
        }
        return;
    }

    if (dir_mode == DIR_WAVE_CLEAR || dir_mode == DIR_BOSS_VICTORY) {
        uint16_t elapsed = dir_total - dir_timer;
        for (int i = 0; i < ALLY_N; ++i) {
            Actor& a = allies[i];
            if (!a.alive || a.state == A_DOWN) continue;
            a.state = A_VICTORY; a.dir = 1;
            integrate_vel(a, 24.0f, STAGE_W - 24.0f, 0.80f);
            if (a.z <= 0.0f && ((elapsed % 24) == (uint16_t)(i * 6))) {
                a.z = 0.8f; a.vz = (dir_mode == DIR_BOSS_VICTORY) ? 3.4f : 2.6f; set_squash(a, 1, 5);
            }
        }
        if (dir_mode == DIR_BOSS_VICTORY) {
            if (elapsed == 8 || elapsed == 15 || elapsed == 22 || elapsed == 29 || elapsed == 36) {
                spawn_vfx_sprite(boss_down_x + frand(-28, 28), boss_down_y - frand(14, 52), 0.0f,
                                 vfx_sprites::FXS_EXPLODE_0, 6, 3, 1, 1, 0, true, 280);
                add_shake(4, 8);
            }
            if (elapsed == 22) {
                spawn_crystal_gems(boss_down_x, boss_down_y, 12.0f, 14);
                spawn_confetti_burst(boss_down_x, boss_down_y - 24.0f, 16.0f, 24);
            }
            if (elapsed > 44 && (elapsed % 12 == 0)) {
                spawn_confetti_burst(cam_x + frand(60.0f, FB_W - 60.0f), BELT_Y0 + 30.0f, 160.0f, 3);
            }
        }
        fx_step();
        if (--dir_timer == 0) {
            bool was_boss = (dir_mode == DIR_BOSS_VICTORY);
            wave_n++;
            if (wave_n > 50) wave_n = 50;
            game::st.wave_reached = wave_n;
            for (auto& e : enemies) e = Actor();
            /* ローグライト仕様: ウェーブ間は部分回復 (通常12% / ボス撃破45% + CAMP TEA) */
            float hfrac = (was_boss ? 0.45f : (gp::g.wave_heal_pct / 100.0f)) + (game::st.mods.wave_heal_pct / 100.0f);
            for (int i = 0; i < ALLY_N; ++i) {
                Actor& a = allies[i];
                if (!a.alive) continue;
                if (a.state == A_DOWN) {
                    a.state = A_IDLE; a.hp = a.hpmax * 0.35f;
                    a.x = cam_x + 90.0f + i * 36.0f;
                } else {
                    a.state = A_IDLE;
                    a.hp = fminf(a.hpmax, a.hp + a.hpmax * hfrac);
                }
                a.poison_t = a.burn_t = a.slow_t = 0;
                a.z = 0.0f; a.vz = 0.0f; a.vx = 0.0f; a.vy = 0.0f;
                a.invuln = true; a.invuln_t = 24;
            }
            dir_mode = DIR_BATTLE;
            wave_timer = 22;
        }
        return;
    }

    /* 通常戦闘進行 */
    for (int i = 0; i < ALLY_N; ++i) if (allies[i].alive) ally_step(allies[i], i);
    for (auto& e : enemies) if (e.alive) enemy_step(e);
    fx_step();

    for (auto& e : enemies) {
        if (e.alive && e.type == E_BOSS) {
            if (boss_hp_trail > e.hp) {
                boss_hp_trail -= (boss_hp_trail - e.hp) * 0.08f + 0.6f;
                if (boss_hp_trail < e.hp) boss_hp_trail = e.hp;
            } else {
                boss_hp_trail = e.hp;
            }
            break;
        }
    }

    /* カメラ追従 */
    const Actor* hero = nullptr;
    for (int k = 0; k < ALLY_N; k++) {
        if (allies[k].alive && allies[k].state != A_DOWN) { hero = &allies[k]; break; }
    }
    if (hero) {
        bool need_pan = (hero->x < cam_x + 68.0f || hero->x > cam_x + (FB_W - 68.0f));
        Actor* foe = nearest_enemy(hero->x, hero->y);
        if (!need_pan && foe && (foe->x < cam_x + 36.0f || foe->x > cam_x + (FB_W - 36.0f))) need_pan = true;
        if (need_pan) {
            float focus_x = hero->x + hero->dir * 40.0f;
            if (foe && fabsf(foe->x - hero->x) < 260.0f) focus_x = hero->x * 0.65f + foe->x * 0.35f;
            float target_cam = focus_x - FB_W * 0.45f;
            if (target_cam < 0.0f) target_cam = 0.0f; if (target_cam > STAGE_W - FB_W) target_cam = STAGE_W - FB_W;
            float diff = target_cam - cam_x;
            if (fabsf(diff) > 16.0f) {
                float step_px = (diff > 0.0f) ? fminf(diff, 6.0f) : fmaxf(diff, -6.0f);
                cam_x = roundf(cam_x + step_px);
            }
        }
        if (cam_x > hero->x - 56.0f) cam_x = roundf(hero->x - 56.0f);
        if (cam_x < hero->x - (FB_W - 56.0f)) cam_x = roundf(hero->x - (FB_W - 56.0f));
        if (cam_x < 0.0f) cam_x = 0.0f; if (cam_x > STAGE_W - FB_W) cam_x = STAGE_W - FB_W;
    }

    int alive_allies = 0;
    for (auto& a : allies) if (a.alive && a.state != A_DOWN) alive_allies++;
    int alive = 0;
    for (auto& e : enemies) if (e.alive) alive++;

    if (wave_active) {
        if (alive_allies == 0) {
            /* PHOENIX ASH チャーム: 1ラン1度だけ全滅蘇生 */
            if (game::st.mods.phoenix > 0 && !game::st.phoenix_used) {
                game::st.phoenix_used = true;
                game::save();
                for (auto& a : allies) {
                    if (!a.alive) continue;
                    a.state = A_IDLE; a.hp = a.hpmax * 0.50f;
                    a.poison_t = a.burn_t = a.slow_t = 0;
                    a.invuln = true; a.invuln_t = 70;
                    spawn_vfx_sprite(a.x, a.y - 6, 0.0f, vfx_sprites::FXS_SHOCKWAVE_0, 4, 3, 1, 0, 0, true, 240);
                }
                special_flash = 3;
                flash_message("PHOENIX REVIVE!!", 65);
            } else {
                wave_active = false;
                for (auto& e : enemies) { e.alive = false; e.state = A_IDLE; e.timer = 0; }
                for (auto& f : fxs) if (f.kind == FX_EBOLT || f.kind == FX_HAZARD) f.on = false;
                game::on_run_end(wave_n, false);
                flash_frames = 0; phase_banner_t = 0; cutin_draw = 0;
                dir_mode = DIR_GAME_OVER;
                dir_timer = 0; dir_total = 1;
                invalidate_all_bands();
            }
        } else if (alive == 0) {
            wave_active = false;
            bool was_boss = (wave_n % 5 == 0);
            double clear_bonus = was_boss ? (65.0 + wave_n * 22.0) : (16.0 + wave_n * 7.2);
            game::add_drops(clear_bonus, party_scavenger_pct());
            win_bonus_shown = clear_bonus;

            for (auto& f : fxs) {
                if (f.on && f.kind == FX_SPR && f.phys_mode == 2 && f.life > 28) f.life = 28;
                if (f.on && (f.kind == FX_HAZARD || f.kind == FX_EBOLT)) f.on = false;
            }
            for (auto& a : allies) if (a.alive && a.state != A_DOWN) { a.state = A_VICTORY; a.dir = 1; }

            if (was_boss) game::on_boss_slain(wave_n);

            if (wave_n >= 50) {
                game::on_run_end(50, true);
                flash_frames = 0; phase_banner_t = 0; cutin_draw = 0;
                spawn_confetti_burst(cam_x + FB_W * 0.5f, BELT_Y0 + 60.0f, 90.0f, 24);
                dir_mode = DIR_ENDING;
                dir_timer = 0; dir_total = 1;
                invalidate_all_bands();
            } else if (was_boss) {
                dir_mode = DIR_BOSS_VICTORY;
                uint32_t vf = gp::g.victory_frames;
                if (vf < 64) vf = 64; if (vf > 240) vf = 240;
                dir_total = (uint16_t)vf; dir_timer = dir_total;
            } else {
                dir_mode = DIR_WAVE_CLEAR;
                dir_total = 56; dir_timer = 56;
                spawn_confetti_burst(cam_x + FB_W * 0.5f, BELT_Y0 + 60.0f, 80.0f, 10);
            }
        }
    } else if (wave_timer > 0) wave_timer--;
    else start_wave();
}

/* ---------------- バンドレンダラ & アセット初期化 ---------------- */
static uint32_t blink_tick = 0;
static bool blink_on() { return ((blink_tick++) / 4) % 2 == 0; }

#include "actor_sprites.inc"

static TightBounds compute_tight_bounds(const uint16_t* run, const uint16_t* end, int w, int h)
{
    int min_x = w, min_y = h, max_x = 0, max_y = 0;
    while (run < end) {
        int y = *run++, x = *run++, count = *run++, opaque = *run++;
        if (x < min_x) min_x = x; if (x + count > max_x) max_x = x + count;
        if (y < min_y) min_y = y; if (y + 1 > max_y) max_y = y + 1;
        run += count * (opaque ? 1 : 2);
    }
    if (max_x <= min_x || max_y <= min_y) return {0, 0, (uint8_t)w, (uint8_t)h};
    if (min_x < 0) min_x = 0; if (min_y < 0) min_y = 0;
    if (max_x > w) max_x = w; if (max_y > h) max_y = h;
    return {(uint8_t)min_x, (uint8_t)min_y, (uint8_t)max_x, (uint8_t)max_y};
}

static void init_ram_assets()
{
    init_actor_caches();
    if (!prop_spans_ram) {
        size_t prop_words = scenery::META[2].offset + scenery::META[2].words;
        prop_spans_ram = (uint16_t*)heap_caps_malloc(prop_words * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!prop_spans_ram) prop_spans_ram = (uint16_t*)heap_caps_malloc(prop_words * 2, MALLOC_CAP_SPIRAM);
        if (prop_spans_ram) memcpy(prop_spans_ram, scenery::SPANS, prop_words * 2);
    }
    if (!scenery_spans_ram) {
        scenery_spans_ram = (uint16_t*)heap_caps_malloc(sizeof(scenery::SPANS), MALLOC_CAP_SPIRAM);
        if (scenery_spans_ram) memcpy(scenery_spans_ram, scenery::SPANS, sizeof(scenery::SPANS));
    }
    if (!vfx_spans_ram) {
        vfx_spans_ram = (uint16_t*)heap_caps_malloc(sizeof(vfx_sprites::SPANS), MALLOC_CAP_SPIRAM);
        if (vfx_spans_ram) memcpy(vfx_spans_ram, vfx_sprites::SPANS, sizeof(vfx_sprites::SPANS));
    }
    static bool bounds_ready = false;
    if (!bounds_ready) {
        bounds_ready = true;
        for (int aid = 0; aid < 7; ++aid) {
            const SpriteAtlas& atlas = (aid < 3) ? ally_atlases[aid] : enemy_atlases[aid - 3];
            for (int p = 0; p < 36; ++p) {
                const uint16_t* r0 = atlas.spans + atlas.offsets[p];
                const uint16_t* r1 = atlas.spans + atlas.offsets[p + 1];
                actor_bounds[aid][p] = compute_tight_bounds(r0, r1, atlas.size, atlas.size);
            }
        }
        const uint16_t* vbase = vfx_spans_ram ? vfx_spans_ram : vfx_sprites::SPANS;
        for (int i = 0; i < vfx_sprites::FXS_COUNT; ++i) {
            const auto& m = vfx_sprites::META[i];
            vfx_bounds[i] = compute_tight_bounds(vbase + m.offset, vbase + m.offset + m.words, m.w, m.h);
        }
        const uint16_t* sbase = scenery_spans_ram ? scenery_spans_ram : scenery::SPANS;
        for (int i = 0; i < scenery::SC_COUNT; ++i) {
            const auto& m = scenery::META[i];
            scenery_bounds[i] = compute_tight_bounds(sbase + m.offset, sbase + m.offset + m.words, m.w, m.h);
        }
    }
}

static void draw_shadow_band(uint16_t* band, int by, int bh, int sx, int sy, int rx)
{
    int ry = rx / 4; if (ry < 2) ry = 2;
    if (sy + ry <= by || sy - ry >= by + bh) return;
    fill_rect_band(band, by, bh, sx - rx, sy - 2, rx * 2, 4, C_SHADOW1);
    fill_rect_band(band, by, bh, sx - (rx * 2) / 3, sy - 1, (rx * 4) / 3, 2, C_SHADOW0);
}

static inline void draw_vfx_id_band(uint16_t* band, int by, int bh, uint8_t id,
                                    int sx, int sy, int8_t dir, uint8_t flash = 0, int scale_q8 = 256)
{
    if (id >= vfx_sprites::FXS_COUNT) return;
    const auto& m = vfx_sprites::META[id];
    const uint16_t* base = vfx_spans_ram ? vfx_spans_ram : vfx_sprites::SPANS;
    const uint16_t* run = base + m.offset;
    blit_span_band(band, by, bh, run, run + m.words, m.w, m.h, m.ax, m.ay, sx, sy, dir, flash, scale_q8, scale_q8);
}

static inline void draw_scenery_id_band(uint16_t* band, int by, int bh, uint8_t id,
                                        int sx, int sy, int8_t dir = 1)
{
    if (id >= scenery::SC_COUNT) return;
    const auto& m = scenery::META[id];
    const uint16_t* base = (id < 3 && prop_spans_ram) ? prop_spans_ram
                         : (scenery_spans_ram ? scenery_spans_ram : scenery::SPANS);
    const uint16_t* run = base + m.offset;
    blit_span_band(band, by, bh, run, run + m.words, m.w, m.h, m.ax, m.ay, sx, sy, dir, 0, 256, 256);
}

static inline void copy_bg_slice(uint16_t* dst, const uint16_t* src_row, int gx0, int x0, int x1)
{
    int gx = (gx0 + x0) & 511;
    int len = x1 - x0;
    dst += x0;
    int first = 512 - gx;
    if (first >= len) {
        memcpy(dst, src_row + gx, (size_t)len * 2);
    } else {
        memcpy(dst, src_row + gx, (size_t)first * 2);
        memcpy(dst + first, src_row, (size_t)(len - first) * 2);
    }
}
static void copy_sky_slice_band(uint16_t* band, int by, int bh, int x0, int x1, int sx0, int oy)
{
    if (!sky_buf) return;
    for (int ry = 0; ry < bh; ++ry) {
        int wy = (by + ry) - oy; if (wy < 0) wy = 0;
        if (wy < scenery::SKY_H) copy_bg_slice(band + ry * FB_W, sky_buf + wy * scenery::SKY_W, sx0, x0, x1);
    }
}
static void copy_gnd_slice_band(uint16_t* band, int by, int bh, int x0, int x1, int gx0, int oy)
{
    if (!gnd_buf) return;
    for (int ry = 0; ry < bh; ++ry) {
        int wy = (by + ry) - oy; if (wy >= FB_H) wy = FB_H - 1;
        if (wy >= scenery::SKY_H) {
            int gy = wy - scenery::SKY_H; if (gy >= scenery::GND_H) gy = scenery::GND_H - 1;
            copy_bg_slice(band + ry * FB_W, gnd_buf + gy * scenery::GND_W, gx0, x0, x1);
        }
    }
}
static void scenery_tint_slice_band(uint16_t* band, int bh, int x0, int x1, uint8_t cur_tint)
{
    if (cur_tint == 0) return;
    uint16_t t = (cur_tint == 1) ? RGB(0x28, 0x06, 0x18)
               : ((cur_tint == 3) ? RGB(0x38, 0x64, 0x78) : RGB(0x06, 0x10, 0x28));
    uint32_t tint32 = ((uint32_t)t << 16) | t;
    int pairs = (x1 - x0) >> 1;
    for (int ry = 0; ry < bh; ++ry) {
        uint32_t* p32 = (uint32_t*)(band + ry * FB_W + x0);
        for (int i = 0; i < pairs; ++i) p32[i] = ((p32[i] >> 1) & 0x7BEF7BEF) + tint32;
    }
}

struct PreparedProp {
    int px, py, w, h, ax, ay;
    uint16_t tmask;
    const uint16_t* cur_run;
    const uint16_t* end_run;
};
struct PreparedActor {
    const Actor* a;
    const uint16_t* cur_run;
    const uint16_t* end_run;
    int size, sx, sy, iz, sx_q8, sy_q8, x0, x1, y_top, y_bot;
};
struct PreparedFx {
    const Fx* f;
    const uint16_t* cur_run;
    const uint16_t* end_run;
    int sx, sy, x0, x1, y_top, y_bot;
    uint8_t spr_id;
    int8_t dir;
    int16_t scale_q8;
};

static void draw_fx_band(uint16_t* band, int by, int bh, PreparedFx& pf)
{
    if (pf.cur_run) {
        const auto& m = vfx_sprites::META[pf.spr_id];
        blit_span_band_cursor(band, by, bh, pf.cur_run, pf.end_run,
                              m.w, m.h, m.ax, m.ay, pf.sx, pf.sy, pf.dir, 0, pf.scale_q8, pf.scale_q8);
        return;
    }
    const Fx& f = *pf.f;
    int sx = pf.sx, sy = pf.sy;
    switch (f.kind) {
    case FX_DMG_NUM: {
        int scale = (f.sub == 1 && f.life > f.max_life - 6) ? 3 : 2;
        uint16_t col = (f.v < 0) ? C_RED : (f.sub == 1 ? C_GOLD : C_WHITE);
        draw_number_shadow_band(band, by, bh, sx - 6, sy - 4, f.v, scale, col);
        break;
    }
    case FX_HEAL_NUM: {
        char buf[16]; snprintf(buf, sizeof(buf), "+%d", (int)f.v);
        draw_text_outline_band(band, by, bh, sx - 8, sy, buf, 2, C_MINT);
        break;
    }
    case FX_DROP_TEXT: {
        char buf[16]; snprintf(buf, sizeof(buf), "+%d", (int)f.v);
        draw_text_outline_band(band, by, bh, sx - 6, sy, buf, 2, C_AQUA);
        break;
    }
    case FX_TELEGRAPH: {
        bool pulse = ((f.life / 3) % 2) == 0;
        draw_ellipse_ring_band(band, by, bh, sx, sy, (int)f.vx, (int)f.vy,
                               pulse ? C_WHITE : C_RED, RGB(0x80, 0x10, 0x20), true);
        break;
    }
    case FX_HAZARD: {
        bool pulse = ((f.life / 5) % 2) == 0;
        uint16_t ec = (f.sub == 0) ? (pulse ? C_LIME : C_GOLD) : (f.sub == 1 ? (pulse ? C_ORANGE : C_RED) : (pulse ? C_WHITE : C_AQUA));
        uint16_t fc = (f.sub == 0) ? RGB(0x28, 0x80, 0x24) : (f.sub == 1 ? RGB(0x98, 0x28, 0x10) : RGB(0x28, 0x70, 0xA8));
        draw_ellipse_ring_band(band, by, bh, sx, sy, (int)f.vx, (int)f.vy, ec, fc, true);
        break;
    }
    case FX_EBOLT: {
        if (f.sub == 1) { /* 迫撃溶岩弾 */
            fill_rect_band(band, by, bh, sx - 7, sy - 7, 14, 14, C_BLACK);
            fill_rect_band(band, by, bh, sx - 5, sy - 5, 10, 10, C_ORANGE);
            fill_rect_band(band, by, bh, sx - 2, sy - 2, 4, 4, C_GOLD);
        } else { /* 呪い針 / 氷結弾 */
            uint16_t c = (f.sub == 2) ? C_AQUA : C_VIOLET;
            fill_rect_band(band, by, bh, sx - 8, sy - 3, 16, 6, C_BLACK);
            fill_rect_band(band, by, bh, sx - 6, sy - 2, 12, 4, c);
            fill_rect_band(band, by, bh, sx + (f.dir > 0 ? 2 : -4), sy - 1, 3, 2, C_WHITE);
        }
        break;
    }
    default: break;
    }
}

/* ---------------- オーバーレイ (ボスHP/Phase通知/カットイン/タイトル/OP/GameOver/Ending) ---------------- */
static void draw_overlays_band(uint16_t* band, int by, int bh)
{
    /* 1. ボス HP バー + 50% / 30% / 10% しきい値マーク (UX ガイドライン: 2x = 14px) */
    if (wave_active && dir_mode == DIR_BATTLE) {
        const Actor* boss = nullptr;
        for (const auto& e : enemies) if (e.alive && e.type == E_BOSS) { boss = &e; break; }
        if (boss && by < 44) {
            const auto& bd = game::BOSSES[boss->talent_id];
            char bname[28];
            snprintf(bname, sizeof(bname), "%s %s", bd.name1, bd.name2);
            draw_text_outline_band(band, by, bh, (FB_W - text_w(bname, 2)) / 2, 4, bname, 2, C_GOLD);
            fill_rect_band(band, by, bh, 52, 22, 376, 14, C_BLACK);
            fill_rect_band(band, by, bh, 53, 23, 374, 12, RGB(0x28, 0x10, 0x22));
            if (boss->hpmax > 0.0f) {
                int tw = (int)(370.0f * boss_hp_trail / boss->hpmax);
                if (tw > 370) tw = 370;
                if (tw > 0) fill_rect_band(band, by, bh, 55, 25, tw, 8, C_GOLD);
                int hw = (int)(370.0f * boss->hp / boss->hpmax);
                if (hw > 370) hw = 370;
                if (hw > 0) fill_rect_band(band, by, bh, 55, 25, hw, 8, C_RED);
                static const int TH_X[3] = {55 + 185, 55 + 111, 55 + 37};
                for (int t = 0; t < 3; ++t) {
                    bool triggered = (boss->boss_phase_mask & (1u << t)) != 0;
                    fill_rect_band(band, by, bh, TH_X[t] - 1, 22, 2, 14, triggered ? RGB(0x40, 0x40, 0x50) : C_WHITE);
                }
            }
        }
    }

    /* ボス Phase 50/30/10% 発動コールアウトバナー */
    if (phase_banner_t > 0 && by < 72 && by + bh > 36) {
        fill_rect_band(band, by, bh, 24, 38, FB_W - 48, 28, RGB(0x42, 0x0C, 0x1E));
        draw_rect_frame_band(band, by, bh, 24, 38, FB_W - 48, 28, 2, C_GOLD);
        int pw = text_w(phase_banner_txt, 2);
        draw_text_shadow_band(band, by, bh, (FB_W - pw) / 2, 45, phase_banner_txt, 2, C_WHITE);
    }

    /* 2. 味方必殺技カットイン */
    if (cutin_draw > 0) {
        static const uint16_t ACCENT[3] = {C_AQUA, C_VIOLET, C_GOLD};
        static const uint16_t BACK[3]   = {RGB(0x0C, 0x1E, 0x42), RGB(0x1E, 0x10, 0x40), RGB(0x36, 0x1E, 0x0C)};
        static const uint8_t PORT[3]    = {scenery::SC_CUTIN_LUMI, scenery::SC_CUTIN_MIRA, scenery::SC_CUTIN_GORO};
        uint8_t who = cutin_who_atlas % 3;
        uint16_t ac = game::TALENTS[cutin_who_tid % game::TALENT_N].use_orig_pal ? ACCENT[who]
                    : rarity_color(game::TALENTS[cutin_who_tid % game::TALENT_N].rarity);
        int slide = 0;
        if (cutin_draw > 42) slide = (cutin_draw - 42) * 80;
        else if (cutin_draw < 6) slide = -(6 - cutin_draw) * 80;
        if (by < 104 && by + bh > 8) {
            fill_rect_band(band, by, bh, slide, 10, FB_W, 86, BACK[who]);
            int spd_off = (int)((frame * 26) % 180);
            for (int l = 0; l < 2; ++l) {
                int lx = slide + ((l * 130 + 480 - spd_off) % 480);
                fill_rect_band(band, by, bh, lx, (l == 0) ? 16 : 87, 96, 2, ac);
            }
            fill_rect_band(band, by, bh, slide, 10, FB_W, 3, ac);
            fill_rect_band(band, by, bh, slide, 93, FB_W, 3, ac);
            draw_scenery_id_band(band, by, bh, PORT[who], slide + 56, 53, 1);
            draw_text_shadow_band(band, by, bh, slide + 114, 24, cutin_who_name, 2, ac);
            draw_text_shadow_band(band, by, bh, slide + 114, 52, cutin_name, 2, C_WHITE);
        }
    }

    /* 3. ボス出現カットイン */
    if (dir_mode == DIR_BOSS_INTRO) {
        uint16_t elapsed = dir_total - dir_timer;
        const auto& bd = game::BOSSES[active_boss_id];
        if (by < 28 || by + bh > 324) {
            fill_rect_band(band, by, bh, 0, 2, FB_W, 24, RGB(0x38, 0x06, 0x12));
            fill_rect_band(band, by, bh, 0, 326, FB_W, 24, RGB(0x38, 0x06, 0x12));
            fill_rect_band(band, by, bh, 0, 26, FB_W, 2, C_RED);
            fill_rect_band(band, by, bh, 0, 324, FB_W, 2, C_RED);
            int scroll = (int)((elapsed * 6) % 180);
            for (int x = -scroll; x < FB_W; x += 180) {
                draw_text_shadow_band(band, by, bh, x, 7, "WARNING!", 2, C_GOLD);
                draw_text_shadow_band(band, by, bh, x, 331, "WARNING!", 2, C_GOLD);
            }
        }
        if (elapsed >= 28 && by < 232 && by + bh > 100) {
            int slide = 0;
            if (elapsed < 34) slide = (34 - elapsed) * 80;
            else if (dir_timer < 6) slide = -(6 - dir_timer) * 80;
            fill_rect_band(band, by, bh, slide, 104, FB_W, 118, RGB(0x1E, 0x08, 0x26));
            fill_rect_band(band, by, bh, slide, 104, FB_W, 3, C_RED);
            fill_rect_band(band, by, bh, slide, 108, FB_W, 2, C_GOLD);
            fill_rect_band(band, by, bh, slide, 219, FB_W, 3, C_RED);
            fill_rect_band(band, by, bh, slide, 216, FB_W, 2, C_GOLD);
            draw_scenery_id_band(band, by, bh, scenery::SC_CUTIN_BOSS, slide + 72, 162, 1);

            char wbuf[20];
            snprintf(wbuf, sizeof(wbuf), "BOSS W%u/50", (unsigned)wave_n);
            draw_text_shadow_band(band, by, bh, slide + 144, 120, wbuf, 2, C_RED);
            draw_text_shadow_band(band, by, bh, slide + 144, 146, bd.name1, 3, C_WHITE);
            draw_text_shadow_band(band, by, bh, slide + 144, 178, bd.name2, 3, C_GOLD);
        }
    }

    /* 4. 通常ウェーブクリア / ボス勝利 */
    if (dir_mode == DIR_WAVE_CLEAR && by < 130 && by + bh > 56) {
        uint16_t elapsed = dir_total - dir_timer;
        int slide = (elapsed < 6) ? (6 - elapsed) * 80 : 0;
        fill_rect_band(band, by, bh, slide, 62, FB_W, 60, RGB(0x10, 0x24, 0x42));
        fill_rect_band(band, by, bh, slide, 62, FB_W, 2, C_AQUA);
        fill_rect_band(band, by, bh, slide, 120, FB_W, 2, C_AQUA);
        char t1[24], t2[24];
        snprintf(t1, sizeof(t1), "WAVE %u CLEAR!", (unsigned)wave_n);
        snprintf(t2, sizeof(t2), "+%d CRYSTALS", (int)win_bonus_shown);
        draw_text_shadow_band(band, by, bh, slide + (FB_W - text_w(t1, 2)) / 2, 72, t1, 2, C_GOLD);
        draw_text_shadow_band(band, by, bh, slide + (FB_W - text_w(t2, 2)) / 2, 96, t2, 2, C_AQUA);
    }

    if (dir_mode == DIR_BOSS_VICTORY) {
        uint16_t elapsed = dir_total - dir_timer;
        if (elapsed < 18 && by < 160 && by + bh > 80) {
            const char* fin = "BOSS DOWN!";
            draw_text_outline_band(band, by, bh, (FB_W - text_w(fin, 4)) / 2, 104, fin, 4, C_GOLD);
        } else if (elapsed >= 48 && by < 180 && by + bh > 64) {
            int slide = (elapsed < 56) ? (56 - elapsed) * 60 : 0;
            fill_rect_band(band, by, bh, slide, 68, FB_W, 98, RGB(0x10, 0x26, 0x46));
            fill_rect_band(band, by, bh, slide, 68, FB_W, 3, C_GOLD);
            fill_rect_band(band, by, bh, slide, 163, FB_W, 3, C_GOLD);
            draw_scenery_id_band(band, by, bh, scenery::SC_CUTIN_LUMI_WIN, slide + 56, 117, 1);
            char b1[24];
            snprintf(b1, sizeof(b1), "+%d C  +FREE BOX", (int)win_bonus_shown);
            draw_text_shadow_band(band, by, bh, slide + 114, 82, "BOSS DEFEATED!", 2, C_GOLD);
            draw_text_shadow_band(band, by, bh, slide + 114, 110, b1, 2, C_AQUA);
            draw_text_shadow_band(band, by, bh, slide + 114, 136, "TAP BOX TO DRAFT", 2, C_MINT);
        }
    }

    /* 0. TITLE SCREEN (UX ガイドライン準拠: 最小 2x = 14px, 引き算設計) */
    if (dir_mode == DIR_TITLE) {
        const bool pulse = ((dir_timer / 24) & 1) == 0;
        const int bob = (int)(sinf(dir_timer * 0.08f) * 3.0f);

        /* タイトルロゴプレート (y = 12..112) */
        fill_rect_band(band, by, bh, 18, 12, 444, 100, RGB(0x0B, 0x13, 0x24));
        draw_rect_frame_band(band, by, bh, 18, 12, 444, 100, 2, C_AQUA);
        draw_rect_frame_band(band, by, bh, 23, 17, 434, 90, 1, RGB(0x28, 0x4C, 0x7A));
        fill_rect_band(band, by, bh, 18, 12, 6, 6, C_GOLD);
        fill_rect_band(band, by, bh, 456, 12, 6, 6, C_GOLD);
        fill_rect_band(band, by, bh, 18, 106, 6, 6, C_GOLD);
        fill_rect_band(band, by, bh, 456, 106, 6, 6, C_GOLD);

        draw_vfx_id_band(band, by, bh, vfx_sprites::FXS_CRYSTAL_GEM_0, 46, 48 + bob, 1, 0, 256);
        draw_vfx_id_band(band, by, bh, vfx_sprites::FXS_CRYSTAL_GEM_0, 434, 48 - bob, 1, 0, 256);

        /* ロゴ: CRYSTAL (AQUA) + GARDEN (GOLD) at scale=4 (28px 高 = 4.2mm) */
        const int w_cry = text_w("CRYSTAL", 4);
        const int w_gar = text_w("GARDEN", 4);
        const int lx0 = (FB_W - (w_cry + 14 + w_gar)) / 2;
        draw_text_outline_band(band, by, bh, lx0, 28, "CRYSTAL", 4, C_AQUA);
        draw_text_outline_band(band, by, bh, lx0 + w_cry + 14, 28, "GARDEN", 4, C_GOLD);

        const char* sub = "LUMI'S SANCTUARY";
        draw_text_shadow_band(band, by, bh, (FB_W - text_w(sub, 2)) / 2, 74, sub, 2, C_WHITE);

        /* 下部アクションボタン (y = 264..330, 高 66px = 9.9mm) */
        if (has_saved_progress()) {
            uint16_t c_edge = pulse ? C_GOLD : C_AQUA;
            fill_rect_band(band, by, bh, 20, 264, 264, 66, RGB(0x14, 0x42, 0x5C));
            draw_rect_frame_band(band, by, bh, 20, 264, 264, 66, 2, c_edge);
            const char* c1 = "CONTINUE";
            draw_text_shadow_band(band, by, bh, 20 + (264 - text_w(c1, 3)) / 2, 274, c1, 3, C_WHITE);
            char c2[24];
            snprintf(c2, sizeof(c2), "RUN#%u W%u/50", (unsigned)game::st.run_number, (unsigned)wave_n);
            draw_text_band(band, by, bh, 20 + (264 - text_w(c2, 2)) / 2, 304, c2, 2, C_MINT);

            fill_rect_band(band, by, bh, 296, 264, 164, 66, RGB(0x36, 0x1A, 0x34));
            draw_rect_frame_band(band, by, bh, 296, 264, 164, 66, 2, RGB(0xC8, 0x6C, 0x98));
            const char* n1 = "NEW RUN";
            draw_text_shadow_band(band, by, bh, 296 + (164 - text_w(n1, 2)) / 2, 276, n1, 2, C_WHITE);
            const char* n2 = "WAVE 1";
            draw_text_band(band, by, bh, 296 + (164 - text_w(n2, 2)) / 2, 304, n2, 2, RGB(0xE8, 0xB8, 0xD0));
        } else {
            uint16_t s_edge = pulse ? C_GOLD : C_AQUA;
            uint16_t s_bg   = pulse ? RGB(0x18, 0x4A, 0x68) : RGB(0x12, 0x38, 0x52);
            fill_rect_band(band, by, bh, 64, 266, 352, 62, s_bg);
            draw_rect_frame_band(band, by, bh, 64, 266, 352, 62, 2, s_edge);
            draw_rect_frame_band(band, by, bh, 68, 270, 344, 54, 1, RGB(0x40, 0x80, 0xA8));
            const char* st = "TAP TO START";
            draw_text_shadow_band(band, by, bh, (FB_W - text_w(st, 3)) / 2, 287, st, 3, C_WHITE);
        }
        return;
    }

    /* 0b. OPENING ANIMATION (UX ガイドライン準拠: 最小 2x = 14px) */
    if (dir_mode == DIR_OPENING) {
        uint16_t f = dir_timer;
        if (f >= 80 && f < 138 && by < 118 && by + bh > 10) {
            int slide = 0;
            if (f < 86) slide = (86 - f) * 80;
            else if (f > 132) slide = -(f - 132) * 80;
            fill_rect_band(band, by, bh, slide, 16, FB_W, 92, RGB(0x22, 0x08, 0x1E));
            fill_rect_band(band, by, bh, slide, 16, FB_W, 3, C_RED);
            fill_rect_band(band, by, bh, slide, 105, FB_W, 3, C_RED);
            draw_scenery_id_band(band, by, bh, scenery::SC_CUTIN_BOSS, slide + 68, 62, 1);
            draw_text_shadow_band(band, by, bh, slide + 136, 32, "SHADOW LEGION", 2, C_RED);
            draw_text_shadow_band(band, by, bh, slide + 136, 62, "DARKNESS FALLS!", 2, C_GOLD);
        } else if (f >= 144 && f < 210 && by < 118 && by + bh > 10) {
            int slide = 0;
            if (f < 150) slide = -(150 - f) * 80;
            else if (f > 204) slide = (f - 204) * 80;
            fill_rect_band(band, by, bh, slide, 16, FB_W, 92, RGB(0x0B, 0x20, 0x42));
            fill_rect_band(band, by, bh, slide, 16, FB_W, 3, C_AQUA);
            fill_rect_band(band, by, bh, slide, 105, FB_W, 3, C_GOLD);
            draw_scenery_id_band(band, by, bh, scenery::SC_CUTIN_LUMI, slide + 60, 62, 1);
            draw_text_shadow_band(band, by, bh, slide + 124, 30, "LUMI AWAKENS!", 3, C_AQUA);
            draw_text_shadow_band(band, by, bh, slide + 124, 66, "RECLAIM 50 WAVES!", 2, C_WHITE);
        } else if (f >= 220 && by < 156 && by + bh > 72) {
            int slide = (f < 226) ? (226 - f) * 80 : 0;
            fill_rect_band(band, by, bh, slide, 78, FB_W, 68, RGB(0x0E, 0x24, 0x44));
            fill_rect_band(band, by, bh, slide, 78, FB_W, 3, C_GOLD);
            fill_rect_band(band, by, bh, slide, 143, FB_W, 3, C_AQUA);
            const char* b1 = "WAVE 1 BEGIN!";
            draw_text_outline_band(band, by, bh, slide + (FB_W - text_w(b1, 3)) / 2, 92, b1, 3, C_GOLD);
            const char* b2 = "DEFEAT 50 WAVES";
            draw_text_shadow_band(band, by, bh, slide + (FB_W - text_w(b2, 2)) / 2, 120, b2, 2, C_AQUA);
        }
        return;
    }

    /* 5. GAME OVER (全滅 / UX ガイドライン準拠: 最小 2x = 14px) */
    if (dir_mode == DIR_GAME_OVER) {
        fill_rect_band(band, by, bh, 18, 16, FB_W - 36, 320, RGB(0x1C, 0x0C, 0x16));
        draw_rect_frame_band(band, by, bh, 18, 16, FB_W - 36, 320, 3, C_RED);
        draw_text_shadow_band(band, by, bh, (FB_W - text_w("DEFEATED", 3)) / 2, 30, "DEFEATED", 3, C_RED);

        char l1[28], l2[28], l3[24];
        snprintf(l1, sizeof(l1), "RUN#%u  WAVE %u/50", (unsigned)game::st.run_number, (unsigned)wave_n);
        draw_text_shadow_band(band, by, bh, (FB_W - text_w(l1, 2)) / 2, 68, l1, 2, C_WHITE);

        snprintf(l2, sizeof(l2), "BEST W%u/50  CLR %u", (unsigned)game::st.best_wave, (unsigned)game::st.total_clears);
        draw_text_band(band, by, bh, (FB_W - text_w(l2, 2)) / 2, 92, l2, 2, C_GOLD);

        for (int i = 0; i < ALLY_N; ++i) {
            int cx = 32 + i * 142;
            fill_rect_band(band, by, bh, cx, 120, 132, 74, RGB(0x14, 0x16, 0x24));
            draw_rect_frame_band(band, by, bh, cx, 120, 132, 74, 1, RGB(0x48, 0x38, 0x58));
            if (game::st.party[i].active) {
                const auto& td = game::TALENTS[game::st.party[i].talent_id];
                draw_text_band(band, by, bh, cx + 8, 132, td.name, 2, rarity_color(td.rarity));
                snprintf(l3, sizeof(l3), "*%u %s", (unsigned)game::st.party[i].rank, td.role_tag);
                draw_text_band(band, by, bh, cx + 8, 160, l3, 2, C_WHITE);
            } else {
                draw_text_band(band, by, bh, cx + 36, 148, "EMPTY", 2, RGB(0x70, 0x76, 0x8C));
            }
        }

        if (game::st.last_unlock_t > 0 || game::st.last_unlock_c > 0) {
            snprintf(l2, sizeof(l2), "UNLOCK! +%u HERO +%u CHM",
                     (unsigned)game::st.last_unlock_t, (unsigned)game::st.last_unlock_c);
            draw_text_shadow_band(band, by, bh, (FB_W - text_w(l2, 2)) / 2, 212, l2, 2, C_MINT);
        } else {
            snprintf(l2, sizeof(l2), "CODEX %d/64  CHM %d",
                     game::discovered_talent_count(), game::total_charms_count());
            draw_text_band(band, by, bh, (FB_W - text_w(l2, 2)) / 2, 212, l2, 2, C_AQUA);
        }

        fill_rect_band(band, by, bh, 72, 256, 336, 56, RGB(0x70, 0x20, 0x30));
        draw_rect_frame_band(band, by, bh, 72, 256, 336, 56, 2, C_GOLD);
        const char* btn = "TAP: NEW RUN";
        draw_text_shadow_band(band, by, bh, (FB_W - text_w(btn, 3)) / 2, 274, btn, 3, C_WHITE);
    }

    /* 6. WAVE 50 ENDING (完全制覇エンディング / UX ガイドライン準拠: 最小 2x) */
    if (dir_mode == DIR_ENDING) {
        fill_rect_band(band, by, bh, 14, 14, FB_W - 28, 324, RGB(0x0E, 0x1E, 0x38));
        draw_rect_frame_band(band, by, bh, 14, 14, FB_W - 28, 324, 3, C_GOLD);
        draw_scenery_id_band(band, by, bh, scenery::SC_CUTIN_LUMI_WIN, 72, 78, 1);

        draw_text_shadow_band(band, by, bh, 132, 28, "GARDEN SAVED!", 3, C_GOLD);
        draw_text_shadow_band(band, by, bh, 132, 62, "50 WAVES CLEAR!", 2, C_MINT);
        char st1[28];
        snprintf(st1, sizeof(st1), "RUN#%u  CLRS %u",
                 (unsigned)game::st.run_number, (unsigned)game::st.total_clears);
        draw_text_band(band, by, bh, 132, 88, st1, 2, C_WHITE);

        for (int i = 0; i < ALLY_N; ++i) {
            int cx = 28 + i * 144;
            fill_rect_band(band, by, bh, cx, 130, 134, 76, RGB(0x14, 0x2A, 0x48));
            draw_rect_frame_band(band, by, bh, cx, 130, 134, 76, 1, C_AQUA);
            if (game::st.party[i].active) {
                const auto& td = game::TALENTS[game::st.party[i].talent_id];
                draw_text_shadow_band(band, by, bh, cx + 8, 142, td.name, 2, rarity_color(td.rarity));
                char rb[16];
                snprintf(rb, sizeof(rb), "*%u %s", (unsigned)game::st.party[i].rank, td.role_tag);
                draw_text_band(band, by, bh, cx + 8, 170, rb, 2, C_WHITE);
            } else {
                draw_text_band(band, by, bh, cx + 36, 158, "SOLO!", 2, C_GOLD);
            }
        }

        char ch1[28];
        snprintf(ch1, sizeof(ch1), "CHM %d  CODEX %d/64",
                 game::total_charms_count(), game::discovered_talent_count());
        draw_text_band(band, by, bh, (FB_W - text_w(ch1, 2)) / 2, 222, ch1, 2, C_GOLD);

        fill_rect_band(band, by, bh, 64, 260, 352, 56, RGB(0x1C, 0x5A, 0x38));
        draw_rect_frame_band(band, by, bh, 64, 260, 352, 56, 2, C_GOLD);
        const char* nrun = "TAP: NEXT RUN";
        draw_text_shadow_band(band, by, bh, (FB_W - text_w(nrun, 3)) / 2, 278, nrun, 3, C_WHITE);
    }

    /* 7. 画面内フラッシュメッセージ */
    if (flash_frames > 0 && flash_msg_buf[0] && dir_mode != DIR_TITLE && dir_mode != DIR_OPENING && dir_mode != DIR_GAME_OVER && dir_mode != DIR_ENDING && by < 96 && by + bh > 44) {
        int w = text_w(flash_msg_buf, 2);
        draw_text_outline_band(band, by, bh, (FB_W - w) / 2, 52, flash_msg_buf, 2, RGB(0xFF, 0xC8, 0x60));
    }
}

/* ---------------- メイン戦闘描画 (11本の32行SRAMバンド) ---------------- */
static inline uint16_t x_span_to_mask(int x0, int x1)
{
    if (x0 < 0) x0 = 0; if (x1 > FB_W) x1 = FB_W;
    if (x1 <= x0) return 0;
    int t0 = x0 >> 5, t1 = (x1 - 1) >> 5;
    return (uint16_t)(((1u << (t1 - t0 + 1)) - 1u) << t0);
}

void render(uint16_t* fb, int buf)
{
    int64_t t0 = esp_timer_get_time();
    int b = buf & 1;

    int ox = 0, oy = 0;
    if (shake > 0) {
        oy = (int)(rnd() % (shake_mag * 2 + 1)) - shake_mag;
        if (shake_mag > 2) ox = (int)(rnd() % (shake_mag * 2 + 1)) - shake_mag;
    }
    int icam = (int)(cam_x + 0.5f);
    uint8_t cur_tint = 0;
    if (special_flash > 0) cur_tint = (special_flash & 1) ? 3 : 2;
    else if (dir_mode == DIR_OPENING) {
        if (dir_timer >= 65 && dir_timer < 140) cur_tint = 1;
        else if (dir_timer >= 140 && dir_timer < 215) cur_tint = 2;
    }
    else if (dir_mode == DIR_BOSS_INTRO || dir_mode == DIR_GAME_OVER) cur_tint = 1;
    else if (cutin_draw > 0 || dir_mode == DIR_ENDING) cur_tint = 2;
    bool bg_changed = (icam != prev_icam[b] || ox != prev_ox[b] || oy != prev_oy[b] || cur_tint != prev_tint[b]);

    PreparedActor p_actors[ALLY_N + ENEMY_N];
    int n_act = 0;
    Actor* order[ALLY_N + ENEMY_N];
    int n = 0;
    for (auto& a : allies) if (a.alive) order[n++] = &a;
    for (auto& e : enemies) if (e.alive || (e.state == A_DOWN && e.timer)) order[n++] = &e;
    for (int i = 1; i < n; i++) {
        Actor* k = order[i]; int j = i - 1;
        while (j >= 0 && order[j]->y > k->y) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }

    for (int i = 0; i < n; ++i) {
        const Actor* a = order[i];
        if (!a->ally && a->spawn_t && blink_on()) continue;
        int sx = (int)a->x - icam + ox;
        int sy = (int)a->y + oy;
        if (sx < -96 || sx > FB_W + 96) continue;
        if (a->hit_vib_t > 0 && a->hit_vib_max > 0) {
            int amp = (a->hit_vib_amp * a->hit_vib_t + a->hit_vib_max - 1) / a->hit_vib_max;
            sx += (a->hit_vib_t & 1) ? amp : -amp;
            if (a->z > 2.0f) sy += (a->hit_vib_t & 2) ? (amp / 2) : -(amp / 2);
        }
        int iz = (int)a->z; if (iz < 0) iz = 0;

        int slot_idx = a->ally ? (int)(a - allies) : (ALLY_N + (int)(a - enemies));
        uint32_t words = 0; int size = 0, pose = 0;
        const uint16_t* run = ensure_actor_pose(*a, slot_idx, words, size, pose);
        int sx_q8, sy_q8;
        actor_squash(*a, sx_q8, sy_q8);
        /* ゴーレム骨格ボス (96->128px相当 1.33x) */
        if (!a->ally && a->type == E_BOSS && a->base_atlas == 2) {
            sx_q8 = (sx_q8 * 340) >> 8;
            sy_q8 = (sy_q8 * 340) >> 8;
        }

        int aid = a->ally ? (int)a->base_atlas : (3 + (int)a->base_atlas);
        const TightBounds& tb = actor_bounds[aid][pose];
        int ax = size / 2, ay = size - 3, body_y = sy - iz;

        int rel_x0 = (a->dir > 0) ? ((int)tb.x0 - ax) : (ax - (int)tb.x1);
        int rel_x1 = (a->dir > 0) ? ((int)tb.x1 - ax) : (ax - (int)tb.x0);
        int x0 = sx + floor_div256(rel_x0 * sx_q8) - 2;
        int x1 = sx + floor_div256(rel_x1 * sx_q8 + 255) + 2;

        int sw = size / 3 - iz / 6; if (sw < 6) sw = 6;
        if (sx - sw < x0) x0 = sx - sw; if (sx + sw > x1) x1 = sx + sw;

        int dst_top = body_y - ((ay * sy_q8 + 128) >> 8);
        int y_top = dst_top + (((int)tb.y0 * sy_q8) >> 8) - 2;
        int y_bot = dst_top + (((int)tb.y1 * sy_q8 + 255) >> 8) + 2;
        if (sy - 2 < y_top) y_top = sy - 2; if (sy + 2 > y_bot) y_bot = sy + 2;

        if (!a->ally && a->alive && a->type != E_BOSS && a->hp < a->hpmax) {
            int hw_bar = size / 2, hy = body_y - ((size * sy_q8) >> 8) - 4;
            if (sx - hw_bar / 2 < x0) x0 = sx - hw_bar / 2;
            if (sx - hw_bar / 2 + hw_bar > x1) x1 = sx - hw_bar / 2 + hw_bar;
            if (hy < y_top) y_top = hy; if (hy + 4 > y_bot) y_bot = hy + 4;
        }

        if (x0 < 0) x0 = 0; if (x1 > FB_W) x1 = FB_W;
        if (x1 <= x0 || y_bot <= 0 || y_top >= FB_H) continue;

        PreparedActor& pa = p_actors[n_act++];
        pa.a = a; pa.cur_run = run; pa.end_run = run + words;
        pa.size = size; pa.sx = sx; pa.sy = sy; pa.iz = iz;
        pa.sx_q8 = sx_q8; pa.sy_q8 = sy_q8;
        pa.x0 = x0; pa.x1 = x1; pa.y_top = y_top; pa.y_bot = y_bot;
    }

    PreparedFx p_fx[FX_N];
    int n_fx = 0;
    const uint16_t* vbase = vfx_spans_ram ? vfx_spans_ram : vfx_sprites::SPANS;
    for (const auto& f : fxs) {
        if (!f.on) continue;
        int sx = (int)f.x - icam + ox;
        int sy = (int)(f.y - f.z) + oy;
        if (sx < -88 || sx > FB_W + 88) continue;

        uint8_t sid = 255; int16_t sq8 = 256; int8_t dir = f.dir; bool is_spr = false;
        if (f.kind == FX_SPR) {
            int elapsed = f.max_life - f.life; if (elapsed < 0) elapsed = 0;
            int fi = (elapsed / f.fdelay) % f.nframes;
            if (f.phys_mode == 2 && f.life <= 28) fi = (motion_tick / 3) % 2;
            sid = (uint8_t)(f.spr_id + fi); sq8 = (int16_t)f.scale_q8; is_spr = true;
        } else if (f.kind == FX_BOLT) {
            sid = (uint8_t)(vfx_sprites::FXS_MIRA_BOLT_0 + ((motion_tick / 2) % 3)); is_spr = true;
        } else if (f.kind == FX_CRYSTAL_RAIN) {
            if (f.sub != 0) continue;
            sid = (uint8_t)(vfx_sprites::FXS_MIRA_BOLT_0 + ((motion_tick / 2) % 3));
            dir = (f.vx >= 0) ? 1 : -1; is_spr = true;
        } else if (f.kind == FX_AQUA_STORM) {
            sid = (uint8_t)(vfx_sprites::FXS_MIRA_BOLT_0 + (f.sub % 3)); is_spr = true;
        }

        PreparedFx pf = {};
        pf.f = &f; pf.sx = sx; pf.sy = sy; pf.spr_id = sid; pf.dir = dir; pf.scale_q8 = sq8;
        if (is_spr) {
            if (sid >= vfx_sprites::FXS_COUNT) continue;
            const auto& m = vfx_sprites::META[sid];
            const TightBounds& tb = vfx_bounds[sid];
            pf.cur_run = vbase + m.offset; pf.end_run = pf.cur_run + m.words;
            int rel_x0 = (dir > 0) ? ((int)tb.x0 - m.ax) : ((int)m.ax - (int)tb.x1);
            int rel_x1 = (dir > 0) ? ((int)tb.x1 - m.ax) : ((int)m.ax - (int)tb.x0);
            pf.x0 = sx + floor_div256(rel_x0 * sq8) - 2;
            pf.x1 = sx + floor_div256(rel_x1 * sq8 + 255) + 2;
            int dst_top = sy - ((m.ay * sq8 + 128) >> 8);
            pf.y_top = dst_top + (((int)tb.y0 * sq8) >> 8) - 2;
            pf.y_bot = dst_top + (((int)tb.y1 * sq8 + 255) >> 8) + 2;
        } else if (f.kind == FX_TELEGRAPH || f.kind == FX_HAZARD) {
            int rx = (int)f.vx + 4, ry = (int)f.vy + 4;
            pf.x0 = sx - rx; pf.x1 = sx + rx; pf.y_top = sy - ry; pf.y_bot = sy + ry;
        } else if (f.kind == FX_EBOLT) {
            pf.x0 = sx - 10; pf.x1 = sx + 10; pf.y_top = sy - 10; pf.y_bot = sy + 10;
        } else if (f.kind == FX_DMG_NUM || f.kind == FX_DROP_TEXT || f.kind == FX_HEAL_NUM) {
            pf.x0 = sx - 12; pf.x1 = sx + 96; pf.y_top = sy - 8; pf.y_bot = sy + 24;
        } else { continue; }
        if (pf.x0 < 0) pf.x0 = 0; if (pf.x1 > FB_W) pf.x1 = FB_W;
        if (pf.x1 <= pf.x0 || pf.y_bot <= 0 || pf.y_top >= FB_H) continue;
        p_fx[n_fx++] = pf;
    }

    int pcam_sky = (int)(cam_x * 0.28f), pcam_mid = (int)(cam_x * 0.58f);
    int sx0 = (pcam_sky - ox) & (scenery::SKY_W - 1);
    int gx0 = (icam - ox) & (scenery::GND_W - 1);
    int horizon_y = scenery::SKY_H + oy;

    PreparedProp p_props[6];
    int n_prop = 0;
    {
        static const uint8_t PROPS[3] = {scenery::SC_PROP_ARCH, scenery::SC_PROP_CRYSTAL, scenery::SC_PROP_LANTERN};
        const uint16_t* prop_base = prop_spans_ram ? prop_spans_ram : (scenery_spans_ram ? scenery_spans_ram : scenery::SPANS);
        int first_i = (pcam_mid / 192) - 1;
        for (int i = first_i; i <= first_i + 4 && n_prop < 6; ++i) {
            uint8_t pid = PROPS[((i % 3) + 3) % 3];
            const auto& m = scenery::META[pid]; const TightBounds& tb = scenery_bounds[pid];
            int px = i * 192 - pcam_mid + ox + 48, py = 104 + oy;
            int px0 = px - m.ax + (int)tb.x0, px1 = px - m.ax + (int)tb.x1;
            if (px1 <= 0 || px0 >= FB_W) continue;
            uint16_t tmask = x_span_to_mask(px0 - 2, px1 + 2); if (!tmask) continue;
            PreparedProp& pp = p_props[n_prop++];
            pp.px = px; pp.py = py; pp.w = m.w; pp.h = m.h; pp.ax = m.ax; pp.ay = m.ay;
            pp.tmask = tmask; pp.cur_run = prop_base + m.offset; pp.end_run = pp.cur_run + m.words;
        }
    }

    bool boss_bar_on = false;
    if (wave_active && dir_mode == DIR_BATTLE) {
        for (const auto& e : enemies) if (e.alive && e.type == E_BOSS) { boss_bar_on = true; break; }
    }

    int64_t t1 = esp_timer_get_time();
    prof_restore_us += t1 - t0;

    for (int bi = 0; bi < NUM_BANDS; ++bi) {
        int by = bi * BAND_H;
        int bh = (by + BAND_H > FB_H) ? (FB_H - by) : BAND_H;
        uint16_t cur_mask = 0;

        for (int i = 0; i < n_act; ++i) {
            if (p_actors[i].y_bot > by && p_actors[i].y_top < by + bh)
                cur_mask |= x_span_to_mask(p_actors[i].x0 - 2, p_actors[i].x1 + 2);
        }
        for (int i = 0; i < n_fx; ++i) {
            if (p_fx[i].y_bot > by && p_fx[i].y_top < by + bh)
                cur_mask |= x_span_to_mask(p_fx[i].x0 - 2, p_fx[i].x1 + 2);
        }
        if (boss_bar_on && bi <= 1) cur_mask |= x_span_to_mask(48, 432);
        if ((cutin_draw > 0 && by < 104) ||
            (phase_banner_t > 0 && by < 72 && by + bh > 36) ||
            (dir_mode == DIR_BOSS_INTRO && (by < 28 || by + bh > 324 || (by < 232 && by + bh > 100))) ||
            (dir_mode == DIR_WAVE_CLEAR && by < 130 && by + bh > 56) ||
            (dir_mode == DIR_BOSS_VICTORY && ((by < 160 && by + bh > 80) || (by < 180 && by + bh > 64))) ||
            (dir_mode == DIR_TITLE || dir_mode == DIR_OPENING || dir_mode == DIR_GAME_OVER || dir_mode == DIR_ENDING) ||
            (flash_frames > 0 && dir_mode != DIR_TITLE && dir_mode != DIR_OPENING && dir_mode != DIR_GAME_OVER && dir_mode != DIR_ENDING && by < 92 && by + bh > 46)) {
            cur_mask = 0x7FFFu;
        }

        uint16_t req = bg_changed ? 0x7FFFu : (uint16_t)(cur_mask | prev_tmask[b][bi]);
        prev_tmask[b][bi] = cur_mask;
        if (req == 0) continue;

        int tile_x0[8], tile_x1[8], n_runs = 0;
        {
            uint16_t m = req;
            while (m) {
                int t0_bit = __builtin_ctz((unsigned)m);
                int run_len = __builtin_ctz((unsigned)(~(uint16_t)(m >> t0_bit)));
                int t1_bit = t0_bit + run_len; if (t1_bit > 15) t1_bit = 15;
                m &= (uint16_t)~(((1u << (t1_bit - t0_bit)) - 1u) << t0_bit);
                tile_x0[n_runs] = t0_bit << 5; tile_x1[n_runs] = t1_bit << 5;
                copy_sky_slice_band(band_buf, by, bh, tile_x0[n_runs], tile_x1[n_runs], sx0, oy);
                n_runs++;
            }
        }
        if (by < horizon_y) {
            int clip_h = horizon_y - by; if (clip_h > bh) clip_h = bh;
            if (clip_h > 0) {
                for (int i = 0; i < n_prop; ++i) {
                    if (p_props[i].tmask & req) {
                        blit_span_band_cursor(band_buf, by, clip_h,
                                              p_props[i].cur_run, p_props[i].end_run,
                                              p_props[i].w, p_props[i].h, p_props[i].ax, p_props[i].ay,
                                              p_props[i].px, p_props[i].py, 1, 0, 256, 256);
                    }
                }
            }
        }
        for (int r = 0; r < n_runs; ++r) {
            copy_gnd_slice_band(band_buf, by, bh, tile_x0[r], tile_x1[r], gx0, oy);
            if (cur_tint != 0) scenery_tint_slice_band(band_buf, bh, tile_x0[r], tile_x1[r], cur_tint);
        }
        for (int i = 0; i < n_act; ++i) {
            const PreparedActor& pa = p_actors[i];
            if (pa.sy + 4 <= by || pa.sy - 4 >= by + bh) continue;
            int sw = pa.size / 3 - pa.iz / 6; if (sw < 6) sw = 6;
            draw_shadow_band(band_buf, by, bh, pa.sx, pa.sy, sw);
        }
        for (int i = 0; i < n_fx; ++i) {
            if (p_fx[i].f->layer == 0 && p_fx[i].y_bot > by && p_fx[i].y_top < by + bh) draw_fx_band(band_buf, by, bh, p_fx[i]);
        }
        for (int i = 0; i < n_act; ++i) {
            PreparedActor& pa = p_actors[i];
            if (pa.y_bot <= by || pa.y_top >= by + bh) continue;
            int body_y = pa.sy - pa.iz;
            uint8_t fl = pa.a->flash ? pa.a->flash : ((pa.a->guard_t > 0 && ((frame / 4) & 1)) ? 2 : 0);
            blit_span_band_cursor(band_buf, by, bh, pa.cur_run, pa.end_run,
                                  pa.size, pa.size, pa.size / 2, pa.size - 3,
                                  pa.sx, body_y, pa.a->dir, fl, pa.sx_q8, pa.sy_q8);
            if (!pa.a->ally && pa.a->alive && pa.a->type != E_BOSS && pa.a->hp < pa.a->hpmax) {
                int w = pa.size / 2, hy = body_y - ((pa.size * pa.sy_q8) >> 8) - 4;
                fill_rect_band(band_buf, by, bh, pa.sx - w / 2, hy, w, 4, C_BLACK);
                int hw = (int)((w - 2) * pa.a->hp / pa.a->hpmax);
                if (hw > 0) fill_rect_band(band_buf, by, bh, pa.sx - w / 2 + 1, hy + 1, hw, 2, C_RED);
            }
        }
        for (int i = 0; i < n_fx; ++i) {
            if (p_fx[i].f->layer == 1 && p_fx[i].y_bot > by && p_fx[i].y_top < by + bh) draw_fx_band(band_buf, by, bh, p_fx[i]);
        }
        for (int i = 0; i < n_fx; ++i) {
            if (p_fx[i].f->layer == 2 && p_fx[i].y_bot > by && p_fx[i].y_top < by + bh) draw_fx_band(band_buf, by, bh, p_fx[i]);
        }
        draw_overlays_band(band_buf, by, bh);

        if (req == 0x7FFFu) {
            memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
        } else {
            for (int r = 0; r < n_runs; ++r) {
                int rx0 = tile_x0[r], rx1 = tile_x1[r]; size_t span_bytes = (size_t)(rx1 - rx0) * 2;
                for (int ry = 0; ry < bh; ++ry) memcpy(fb + (by + ry) * FB_W + rx0, band_buf + ry * FB_W + rx0, span_bytes);
            }
        }
    }

    prev_icam[b] = icam; prev_ox[b] = ox; prev_oy[b] = oy; prev_tint[b] = cur_tint;
    prof_draw_us += esp_timer_get_time() - t1;
    prof_n++;
}

void render_stats(int64_t* restore_us, int64_t* draw_us)
{
    uint32_t n = prof_n ? prof_n : 1;
    *restore_us = prof_restore_us / n;
    *draw_us = prof_draw_us / n;
    prof_restore_us = 0; prof_draw_us = 0; prof_n = 0;
}

/* ---------------- HUD (上部ステータス + 毎フレーム更新ミニマップ + BOXボタン) ---------------- */
void render_hud(uint16_t* fb, int buf)
{
    if (!band_buf) return;
    int b_idx = buf & 1;
    HudState& hs = hud_st[b_idx];
    char b[48], f1[24], f2[24];
    const uint16_t C_HUD_BG = RGB(0x10, 0x10, 0x1A);
    const uint16_t C_HUD_LN = RGB(0x2E, 0x36, 0x62);

    /* 0. タイトル画面時は 480x480 統一のタイトル上下フレーム (UX ガイドライン: 最小 2x = 14px) */
    if (dir_mode == DIR_TITLE) {
        bool blink = ((dir_timer / 28) & 1) == 0;
        if (!hs.valid || hs.hud_mode != 1 || hs.title_blink != blink) {
            const uint16_t C_TBG = RGB(0x0A, 0x0E, 0x1A);
            for (int by = 0; by < 56; by += 32) {
                int bh = (by == 0) ? 32 : 24;
                fill_span32(band_buf, bh * FB_W, C_TBG);
                fill_rect_band(band_buf, by, bh, 0, 52, FB_W, 2, RGB(0x24, 0x40, 0x6E));
                fill_rect_band(band_buf, by, bh, 0, 54, FB_W, 2, C_AQUA);
                const char* t2 = "50-WAVE ROGUELITE";
                draw_text_shadow_band(band_buf, by, bh, (FB_W - text_w(t2, 2)) / 2, 20, t2, 2, C_GOLD);
                memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
            }
            for (int by = 408; by < 480; by += 32) {
                int bh = (by + 32 > 480) ? (480 - by) : 32;
                fill_span32(band_buf, bh * FB_W, C_TBG);
                fill_rect_band(band_buf, by, bh, 0, 408, FB_W, 2, C_AQUA);
                fill_rect_band(band_buf, by, bh, 0, 410, FB_W, 2, RGB(0x24, 0x40, 0x6E));
                if (has_saved_progress()) {
                    uint32_t disp_best = (game::st.best_wave > wave_n) ? game::st.best_wave : wave_n;
                    snprintf(b, sizeof(b), "BEST W%u/50  CLR %u",
                             (unsigned)disp_best, (unsigned)game::st.total_clears);
                    draw_text_band(band_buf, by, bh, (FB_W - text_w(b, 2)) / 2, 420, b, 2, C_GOLD);
                } else {
                    const char* tip = "TAP BOX * SWIPE PET";
                    draw_text_band(band_buf, by, bh, (FB_W - text_w(tip, 2)) / 2, 420, tip, 2, C_AQUA);
                }
                const char* prompt = "TOUCH TO PLAY";
                uint16_t pcol = blink ? C_WHITE : RGB(0x60, 0x7A, 0x9C);
                draw_text_shadow_band(band_buf, by, bh, (FB_W - text_w(prompt, 2)) / 2, 448, prompt, 2, pcol);
                memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
            }
            hs.hud_mode = 1;
            hs.title_blink = blink;
            hs.valid = true;
        }
        return;
    }

    /* 0b. OP アニメーション時はシネマスコープ上下黒帯＋ストーリー字幕 (最小 2x = 14px) */
    if (dir_mode == DIR_OPENING) {
        uint8_t sc = (dir_timer < 65) ? 0 : (dir_timer < 140 ? 1 : (dir_timer < 215 ? 2 : 3));
        uint8_t bar = (uint8_t)((dir_timer * 80) / OP_DURATION);
        if (bar > 80) bar = 80;
        if (!hs.valid || hs.hud_mode != 2 || hs.op_scene != sc || hs.op_bar != bar) {
            const uint16_t C_CINE = RGB(0x05, 0x07, 0x0E);
            const uint16_t C_LINE = (sc == 1) ? C_RED : (sc == 2 ? C_AQUA : C_GOLD);
            for (int by = 0; by < 56; by += 32) {
                int bh = (by == 0) ? 32 : 24;
                fill_span32(band_buf, bh * FB_W, C_CINE);
                fill_rect_band(band_buf, by, bh, 0, 54, FB_W, 2, C_LINE);
                draw_text_band(band_buf, by, bh, 14, 20, "PROLOGUE", 2, RGB(0x98, 0xB2, 0xD6));
                draw_text_band(band_buf, by, bh, 274, 20, "SKIP >>", 2, RGB(0x88, 0x9E, 0xBC));
                fill_rect_band(band_buf, by, bh, 384, 23, 82, 10, RGB(0x1C, 0x24, 0x38));
                if (bar > 0) fill_rect_band(band_buf, by, bh, 385, 24, bar, 8, C_LINE);
                memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
            }
            static const char* SUB_L1[4] = {
                "THE CRYSTAL GARDEN",
                "THE SHADOW LEGION",
                "LUMI AWAKENS!",
                "THE BATTLE BEGINS!"
            };
            static const char* SUB_L2[4] = {
                "SHONE IN PEACE...",
                "INVADED THE LAND!",
                "RECLAIM 50 WAVES!",
                "TOWARD WAVE 50..."
            };
            uint16_t col1 = (sc == 1) ? C_RED : (sc == 2 ? C_AQUA : C_GOLD);
            for (int by = 408; by < 480; by += 32) {
                int bh = (by + 32 > 480) ? (480 - by) : 32;
                fill_span32(band_buf, bh * FB_W, C_CINE);
                fill_rect_band(band_buf, by, bh, 0, 408, FB_W, 2, C_LINE);
                draw_text_shadow_band(band_buf, by, bh, (FB_W - text_w(SUB_L1[sc], 2)) / 2, 420, SUB_L1[sc], 2, col1);
                draw_text_shadow_band(band_buf, by, bh, (FB_W - text_w(SUB_L2[sc], 2)) / 2, 448, SUB_L2[sc], 2, C_WHITE);
                memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
            }
            hs.hud_mode = 2;
            hs.op_scene = sc;
            hs.op_bar = bar;
            hs.valid = true;
        }
        return;
    }

    if (hs.hud_mode != 0) {
        hs.hud_mode = 0;
        hs.valid = false;
    }

    /* 1. 上部 HUD (y = 0..55 / UX ガイドライン: 全要素 2x〜3x の引き算設計) */
    const Actor& lead = allies[0];
    int lead_hw = (lead.alive && lead.hpmax > 0 && lead.hp > 0) ? (int)(136 * lead.hp / lead.hpmax) : 0;
    if (lead_hw > 136) lead_hw = 136;

    char crys_line[20];
    snprintf(crys_line, sizeof(crys_line), "C %s", game::fmt(game::st.crystals, f1, sizeof(f1)));

    bool box_ready = game::can_open_box();
    bool draft_open = game::st.draft_open;
    char goal_line[20];
    uint16_t goal_col = RGB(0xB8, 0xC6, 0xDC);
    if (draft_open) {
        snprintf(goal_line, sizeof(goal_line), "PICK BOX!");
        goal_col = C_GOLD;
    } else if (game::st.free_boxes > 0) {
        snprintf(goal_line, sizeof(goal_line), "FREE X%u!", (unsigned)game::st.free_boxes);
        goal_col = C_GOLD;
    } else if (box_ready) {
        snprintf(goal_line, sizeof(goal_line), "BOX READY!");
        goal_col = C_MINT;
    } else {
        snprintf(goal_line, sizeof(goal_line), "BOX %s", game::fmt(game::box_cost(), f2, sizeof(f2)));
    }

    int pet_s = game::pet_remaining_s();
    char meta_r[16];
    if (pet_s > 0) snprintf(meta_r, sizeof(meta_r), "PET %d:%02d", pet_s / 60, pet_s % 60);
    else snprintf(meta_r, sizeof(meta_r), "CHM %d", game::total_charms_count());

    bool top_dirty = !hs.valid || hs.lead_hw != lead_hw || hs.wave != wave_n ||
                     hs.pet_s != pet_s || hs.box_ready != box_ready || hs.draft_open != draft_open ||
                     hs.free_boxes != game::st.free_boxes ||
                     strcmp(hs.crys, crys_line) != 0 || strcmp(hs.goal, goal_line) != 0 ||
                     strcmp(hs.meta_r, meta_r) != 0 || hs.tid[0] != lead.talent_id;

    if (top_dirty) {
        for (int by = 0; by < 56; by += 32) {
            int bh = (by == 0) ? 32 : 24;
            fill_span32(band_buf, bh * FB_W, C_HUD_BG);
            fill_rect_band(band_buf, by, bh, 0, 54, FB_W, 2, C_HUD_LN);

            /* 左: リーダー HP バー (太 20px + 2x 名前) & PET/CHARM 2x */
            fill_rect_band(band_buf, by, bh, 8, 6, 140, 20, C_BLACK);
            if (lead_hw > 0) fill_rect_band(band_buf, by, bh, 10, 8, lead_hw, 16, RGB(0xE0, 0x48, 0x60));
            char lname[10]; snprintf(lname, sizeof(lname), "%.8s", game::TALENTS[lead.talent_id].name);
            draw_text_shadow_band(band_buf, by, bh, 12, 9, lname, 2, C_WHITE);
            draw_text_band(band_buf, by, bh, 8, 33, meta_r, 2, (pet_s > 0) ? RGB(0xF8, 0xA8, 0xC4) : RGB(0xA8, 0xC4, 0xE0));

            /* 中央: クリスタル & BOX 状態 (すべて 2x) */
            draw_vfx_id_band(band_buf, by, bh, vfx_sprites::FXS_CRYSTAL_GEM_0, 168, 16, 1, 0, 256);
            draw_text_band(band_buf, by, bh, 182, 9, crys_line, 2, C_AQUA);
            draw_text_band(band_buf, by, bh, 162, 33, goal_line, 2, goal_col);

            /* 右: WAVE / BOSS (3x = 21px = 3.15mm で一目で視認!) */
            if (wave_n % 5 == 0) {
                snprintf(b, sizeof(b), "BOSS%u", (unsigned)wave_n);
                draw_text_shadow_band(band_buf, by, bh, FB_W - 8 - text_w(b, 3), 16, b, 3, RGB(0xFF, 0x68, 0x70));
            } else {
                snprintf(b, sizeof(b), "W%u/50", (unsigned)wave_n);
                draw_text_shadow_band(band_buf, by, bh, FB_W - 8 - text_w(b, 3), 16, b, 3, C_GOLD);
            }
            memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
        }
        hs.lead_hw = lead_hw; hs.wave = wave_n; hs.pet_s = pet_s;
        snprintf(hs.crys, sizeof(hs.crys), "%s", crys_line);
        snprintf(hs.goal, sizeof(hs.goal), "%s", goal_line);
        snprintf(hs.meta_r, sizeof(hs.meta_r), "%s", meta_r);
    }

    /* 2. 下部パーティカード & BOXボタン変更判定 */
    int hp_i[ALLY_N], hpm_i[ALLY_N], mp_i[ALLY_N], sh_i[ALLY_N];
    uint8_t amask = 0, dmask = 0, smask[ALLY_N] = {}, tids[ALLY_N] = {}, ranks[ALLY_N] = {};
    bool stats_dirty = !hs.valid || hs.box_ready != box_ready || hs.draft_open != draft_open || hs.free_boxes != game::st.free_boxes;
    for (int i = 0; i < ALLY_N; ++i) {
        const auto& a = allies[i];
        hp_i[i]  = a.alive ? (int)ceilf(a.hp) : 0;
        hpm_i[i] = a.alive ? (int)a.hpmax : 1;
        mp_i[i]  = a.alive ? (int)a.rage : 0;
        sh_i[i]  = a.alive ? (int)a.shield : 0;
        if (a.alive) amask |= (1u << i);
        if (a.alive && a.state == A_DOWN) dmask |= (1u << i);
        if (a.poison_t > 0) smask[i] |= 1;
        if (a.burn_t > 0)   smask[i] |= 2;
        if (a.slow_t > 0)   smask[i] |= 4;
        tids[i] = a.talent_id; ranks[i] = a.rank;
        if (hs.hp_int[i] != hp_i[i] || hs.hpmax_int[i] != hpm_i[i] || hs.mp_int[i] != mp_i[i] ||
            hs.sh_int[i] != sh_i[i] || hs.status_mask[i] != smask[i] || hs.tid[i] != tids[i] || hs.rank[i] != ranks[i]) {
            stats_dirty = true;
        }
    }
    if (hs.active_mask != amask || hs.down_mask != dmask) stats_dirty = true;

    /* ミニマップ (y=408..424, x=6..385) は毎フレーム更新 */
    bool map_dirty = true;

    const bool box_lit = box_ready || draft_open;
    const uint16_t box_bg = box_lit ? RGB(0x64, 0x3C, 0x10) : RGB(0x1C, 0x34, 0x4E);
    const uint16_t box_edge = box_lit ? C_GOLD : C_AQUA;

    if (map_dirty || stats_dirty) {
        if (map_dirty) {
            int by = 408, bh = 17;
            fill_span32(band_buf, bh * FB_W, C_HUD_BG);
            fill_rect_band(band_buf, by, bh, 6, 410, 380, 14, C_HUD_LN);
            fill_rect_band(band_buf, by, bh, 7, 411, 378, 12, RGB(0x0A, 0x0E, 0x1A));
            int mx0 = 7 + (int)(cam_x * 378.0f / STAGE_W);
            int mw  = (int)(FB_W * 378.0f / STAGE_W);
            if (mx0 < 7) mx0 = 7; if (mx0 + mw > 385) mw = 385 - mx0;
            fill_rect_band(band_buf, by, bh, mx0, 411, mw, 12, RGB(0x1C, 0x2C, 0x48));
            fill_rect_band(band_buf, by, bh, mx0, 411, 1, 12, RGB(0x60, 0x90, 0xD0));
            fill_rect_band(band_buf, by, bh, mx0 + mw - 1, 411, 1, 12, RGB(0x60, 0x90, 0xD0));

            for (const auto& f : fxs) {
                if (!f.on || f.kind != FX_HAZARD) continue;
                int fx = 7 + (int)(f.x * 374.0f / STAGE_W);
                int fy = 413 + (int)((f.y - BELT_Y0) / (BELT_Y1 - BELT_Y0) * 7.0f);
                fill_rect_band(band_buf, by, bh, fx, fy, 4, 2, (f.sub == 0) ? C_LIME : C_ORANGE);
            }
            for (const auto& e : enemies) {
                if (!e.alive) continue;
                int ex = 7 + (int)(e.x * 374.0f / STAGE_W);
                int ey = 412 + (int)((e.y - BELT_Y0) / (BELT_Y1 - BELT_Y0) * 7.0f);
                if (ex < 7) ex = 7; if (ex > 381) ex = 381; if (ey < 412) ey = 412; if (ey > 419) ey = 419;
                int sz = (e.type == E_BOSS) ? 4 : 3;
                fill_rect_band(band_buf, by, bh, ex, ey, sz, sz, (e.type == E_BOSS) ? RGB(0xFF, 0x30, 0x30) : RGB(0xE8, 0x40, 0x40));
            }
            for (const auto& a : allies) {
                if (!a.alive) continue;
                int ax = 7 + (int)(a.x * 374.0f / STAGE_W);
                int ay = 412 + (int)((a.y - BELT_Y0) / (BELT_Y1 - BELT_Y0) * 7.0f);
                if (ax < 7) ax = 7; if (ax > 381) ax = 381; if (ay < 412) ay = 412; if (ay > 419) ay = 419;
                fill_rect_band(band_buf, by, bh, ax, ay, 3, 3, (a.state == A_DOWN) ? RGB(0x38, 0x60, 0x40) : RGB(0x40, 0xF0, 0x60));
            }
            if (stats_dirty) {
                fill_rect_band(band_buf, by, bh, BOXBTN_X, BOXBTN_Y, BOXBTN_W, BOXBTN_H, box_bg);
                fill_rect_band(band_buf, by, bh, BOXBTN_X, BOXBTN_Y, BOXBTN_W, 2, box_edge);
                memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
            } else {
                for (int ry = 0; ry < bh; ++ry) memcpy(fb + (by + ry) * FB_W, band_buf + ry * FB_W, 388 * 2);
            }
        }

        if (stats_dirty) {
            for (int by = 425; by < 480; by += 32) {
                int bh = (by + 32 > 480) ? (480 - by) : 32;
                fill_span32(band_buf, bh * FB_W, C_HUD_BG);

                /* 下部 3 パーティ枠 (幅 122px x 3, すべて 2x 名前 + 太ゲージ) */
                for (int i = 0; i < ALLY_N; i++) {
                    int x0 = 6 + i * 128;
                    const Actor& a = allies[i];
                    if (!a.alive) {
                        draw_rect_frame_band(band_buf, by, bh, x0, 427, 122, 48, 1, RGB(0x2E, 0x38, 0x52));
                        const char* emp = "+ BOX";
                        draw_text_band(band_buf, by, bh, x0 + (122 - text_w(emp, 2)) / 2, 444, emp, 2, RGB(0x70, 0x84, 0xA4));
                        continue;
                    }
                    const auto& td = game::TALENTS[a.talent_id];
                    bool down = (a.state == A_DOWN);
                    uint16_t acol = down ? RGB(0xB0, 0x58, 0x58) : rarity_color(td.rarity);

                    if (down) {
                        draw_text_band(band_buf, by, bh, x0, 428, "DOWN!", 2, C_RED);
                    } else if (a.poison_t > 0 && ((frame / 20) & 1)) {
                        draw_text_band(band_buf, by, bh, x0, 428, "POISON", 2, C_LIME);
                    } else if (a.burn_t > 0 && ((frame / 20) & 1)) {
                        draw_text_band(band_buf, by, bh, x0, 428, "BURN!", 2, C_ORANGE);
                    } else if (a.slow_t > 0 && ((frame / 20) & 1)) {
                        draw_text_band(band_buf, by, bh, x0, 428, "SLOW", 2, C_AQUA);
                    } else {
                        snprintf(b, sizeof(b), "%.7s*%u", td.name, (unsigned)a.rank);
                        draw_text_band(band_buf, by, bh, x0, 428, b, 2, acol);
                    }

                    /* 太 HP バー (h = 15px) */
                    fill_rect_band(band_buf, by, bh, x0, 446, 122, 15, C_BLACK);
                    if (a.hpmax > 0 && a.hp > 0) {
                        int hw = (int)(120 * a.hp / a.hpmax); if (hw > 120) hw = 120;
                        uint16_t hcol = (a.hp < a.hpmax * 0.3f) ? C_RED : RGB(0x40, 0xD8, 0x68);
                        fill_rect_band(band_buf, by, bh, x0 + 1, 447, hw, 13, hcol);
                        if (a.shield > 0) {
                            int sw = (int)(120 * a.shield / a.hpmax); if (sw > 120) sw = 120;
                            fill_rect_band(band_buf, by, bh, x0 + 1, 447, sw, 4, C_AQUA);
                        }
                    }

                    /* 太 MP / RAGE バー (h = 11px) */
                    fill_rect_band(band_buf, by, bh, x0, 464, 122, 11, C_BLACK);
                    int mw2 = (int)(120 * a.rage / 100.0f); if (mw2 > 120) mw2 = 120;
                    if (mw2 > 0) fill_rect_band(band_buf, by, bh, x0 + 1, 465, mw2, 9, acol);
                }

                fill_rect_band(band_buf, by, bh, BOXBTN_X, BOXBTN_Y, BOXBTN_W, BOXBTN_H, box_bg);
                fill_rect_band(band_buf, by, bh, BOXBTN_X, BOXBTN_Y, BOXBTN_W, 2, box_edge);
                fill_rect_band(band_buf, by, bh, BOXBTN_X, BOXBTN_Y + BOXBTN_H - 2, BOXBTN_W, 2, box_edge);
                const char* l1 = box_lit ? "BOX!" : "BOX";
                const char* l2 = draft_open ? "PICK" : (box_lit ? "OPEN" : "INFO");
                draw_text_shadow_band(band_buf, by, bh, BOXBTN_X + (BOXBTN_W - text_w(l1, 2)) / 2, BOXBTN_Y + 14, l1, 2, box_lit ? C_GOLD : C_WHITE);
                draw_text_shadow_band(band_buf, by, bh, BOXBTN_X + (BOXBTN_W - text_w(l2, 2)) / 2, BOXBTN_Y + 40, l2, 2, box_lit ? C_MINT : C_AQUA);
                memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
            }
            for (int i = 0; i < ALLY_N; ++i) {
                hs.hp_int[i] = hp_i[i]; hs.hpmax_int[i] = hpm_i[i]; hs.mp_int[i] = mp_i[i];
                hs.sh_int[i] = sh_i[i]; hs.status_mask[i] = smask[i]; hs.tid[i] = tids[i]; hs.rank[i] = ranks[i];
            }
            hs.active_mask = amask; hs.down_mask = dmask;
            hs.box_ready = box_ready; hs.draft_open = draft_open; hs.free_boxes = game::st.free_boxes;
        }
    }
    hs.valid = true;
}

/* ---------------- ルートボックス 3択ドラフト / 入替 / ステータス画面 (最小 2x = 14px) ---------------- */
static void draw_draft_card_band(uint16_t* band, int by, int bh, int c_idx)
{
    int x = CARD_X[c_idx], y = CARD_Y;
    const auto& dc = game::st.draft[c_idx];
    uint16_t rc = rarity_color(dc.rarity);
    uint16_t rbg = rarity_bg(dc.rarity);

    fill_rect_band(band, by, bh, x, y, CARD_W, CARD_H, RGB(0x16, 0x1C, 0x2C));
    draw_rect_frame_band(band, by, bh, x, y, CARD_W, CARD_H, 2, rc);
    fill_rect_band(band, by, bh, x + 2, y + 2, CARD_W - 4, 24, rbg);
    const char* rt = game::rarity_tag(dc.rarity);
    draw_text_shadow_band(band, by, bh, x + (CARD_W - text_w(rt, 2)) / 2, y + 7, rt, 2, rc);

    /* プレビュー枠 (h = 84px) */
    fill_rect_band(band, by, bh, x + 8, y + 30, CARD_W - 16, 84, RGB(0x0E, 0x14, 0x22));
    if (dc.is_talent) {
        const auto& td = game::TALENTS[dc.id];
        uint32_t words = 0; int sz = 0;
        int slot = ALLY_N + c_idx;
        const uint16_t* run = ensure_preview_pose(td.base_atlas, 0, td.use_orig_pal != 0, td.pal_pri, td.pal_sec, slot, words, sz);
        blit_span_band(band, by, bh, run, run + words, sz, sz, sz / 2, sz - 3, x + CARD_W / 2, y + 110, 1, 0, 256, 256);

        char nm[12]; snprintf(nm, sizeof(nm), "%.10s", td.name);
        draw_text_shadow_band(band, by, bh, x + (CARD_W - text_w(nm, 2)) / 2, y + 120, nm, 2, rc);

        bool in_party = false; uint8_t cur_r = 0;
        for (int i = 0; i < ALLY_N; ++i) {
            if (game::st.party[i].active && game::st.party[i].talent_id == dc.id) { in_party = true; cur_r = game::st.party[i].rank; }
        }
        char l1[16], l2[16], l3[16];
        if (in_party) snprintf(l1, sizeof(l1), "*%u -> *%u", (unsigned)cur_r, (unsigned)(cur_r + 1));
        else snprintf(l1, sizeof(l1), "%.10s", td.role_tag);
        draw_text_band(band, by, bh, x + (CARD_W - text_w(l1, 2)) / 2, y + 144, l1, 2, C_WHITE);

        snprintf(l2, sizeof(l2), "HP %d", (int)td.hp);
        draw_text_band(band, by, bh, x + 10, y + 170, l2, 2, C_MINT);
        snprintf(l3, sizeof(l3), "ATK %d", (int)(td.dmg + 0.5f));
        draw_text_band(band, by, bh, x + 10, y + 194, l3, 2, C_AQUA);

        const char* cta = in_party ? "RANK UP" : ((game::party_count() < ALLY_N) ? "HIRE" : "SWAP");
        uint16_t cta_bg = in_party ? RGB(0x20, 0x62, 0x42) : ((game::party_count() < ALLY_N) ? RGB(0x1C, 0x50, 0x72) : RGB(0x6E, 0x42, 0x16));
        fill_rect_band(band, by, bh, x + 8, y + 236, CARD_W - 16, 46, cta_bg);
        draw_rect_frame_band(band, by, bh, x + 8, y + 236, CARD_W - 16, 46, 2, rc);
        draw_text_shadow_band(band, by, bh, x + (CARD_W - text_w(cta, 2)) / 2, y + 252, cta, 2, C_WHITE);
    } else {
        const auto& ch = game::CHARMS[dc.id];
        uint8_t lv = game::st.charm_lv[dc.id];
        draw_rect_frame_band(band, by, bh, x + 46, y + 44, 54, 54, 2, rc);
        uint8_t icon = (ch.eff == game::CE_PHOENIX_ASH || ch.eff == game::CE_WAVE_HEAL_PCT || ch.eff == game::CE_REGEN_X10)
                     ? vfx_sprites::FXS_HEART_BUFF
                     : ((dc.rarity >= game::RAR_SR) ? vfx_sprites::FXS_MIRA_BURST_1 : vfx_sprites::FXS_CRYSTAL_GEM_0);
        draw_vfx_id_band(band, by, bh, icon, x + CARD_W / 2, y + 71, 1, 0, 256);

        char nbuf[12]; snprintf(nbuf, sizeof(nbuf), "%.10s", ch.name);
        draw_text_shadow_band(band, by, bh, x + (CARD_W - text_w(nbuf, 2)) / 2, y + 120, nbuf, 2, rc);
        char l1[16]; snprintf(l1, sizeof(l1), "LV%u -> %u", (unsigned)lv, (unsigned)(lv + 1));
        draw_text_band(band, by, bh, x + (CARD_W - text_w(l1, 2)) / 2, y + 144, l1, 2, C_MINT);

        /* 効果説明を 10文字 2行で 2x 折返し表示 */
        char d1[12] = {}, d2[12] = {};
        const char* sp = strchr(ch.desc, ' ');
        if (sp && (sp - ch.desc) <= 10) {
            int n1 = (int)(sp - ch.desc);
            memcpy(d1, ch.desc, n1); d1[n1] = '\0';
            snprintf(d2, sizeof(d2), "%.10s", sp + 1);
        } else {
            snprintf(d1, sizeof(d1), "%.10s", ch.desc);
            if (strlen(ch.desc) > 10) snprintf(d2, sizeof(d2), "%.10s", ch.desc + 10);
        }
        draw_text_band(band, by, bh, x + (CARD_W - text_w(d1, 2)) / 2, y + 174, d1, 2, C_WHITE);
        if (d2[0]) draw_text_band(band, by, bh, x + (CARD_W - text_w(d2, 2)) / 2, y + 198, d2, 2, C_GOLD);

        fill_rect_band(band, by, bh, x + 8, y + 236, CARD_W - 16, 46, RGB(0x28, 0x48, 0x62));
        draw_rect_frame_band(band, by, bh, x + 8, y + 236, CARD_W - 16, 46, 2, rc);
        const char* cta = "EQUIP";
        draw_text_shadow_band(band, by, bh, x + (CARD_W - text_w(cta, 2)) / 2, y + 252, cta, 2, C_WHITE);
    }
}

void render_panel(uint16_t* fb, bool open, bool info_peek, int)
{
    if (!open || !band_buf) return;
    const uint16_t C_PAN_BG = RGB(0x12, 0x14, 0x22);
    int swap_tid = game::st.swap_talent;
    bool show_swap = game::st.draft_open && (swap_tid >= 0);
    bool show_draft = game::st.draft_open && !info_peek && !show_swap;

    for (int by = PANEL_Y; by < PANEL_Y + PANEL_H; by += 32) {
        int bh = (by + 32 > PANEL_Y + PANEL_H) ? (PANEL_Y + PANEL_H - by) : 32;
        fill_span32(band_buf, bh * FB_W, C_PAN_BG);
        fill_rect_band(band_buf, by, bh, PANEL_X, PANEL_Y, PANEL_W, 2, RGB(0x2A, 0x2C, 0x52));

        /* ヘッダ [X] ボタン (60 x 44 px) */
        fill_rect_band(band_buf, by, bh, CLOSE_X, CLOSE_Y, CLOSE_W, CLOSE_H, RGB(0x68, 0x28, 0x28));
        draw_rect_frame_band(band_buf, by, bh, CLOSE_X, CLOSE_Y, CLOSE_W, CLOSE_H, 2, C_RED);
        draw_text_shadow_band(band_buf, by, bh, CLOSE_X + (CLOSE_W - text_w("X", 3)) / 2, CLOSE_Y + 11, "X", 3, C_WHITE);

        if (show_swap) {
            draw_text_shadow_band(band_buf, by, bh, 14, PANEL_Y + 12, "SWAP HERO", 3, C_GOLD);
            const auto& nt = game::TALENTS[swap_tid % game::TALENT_N];
            fill_rect_band(band_buf, by, bh, 12, PANEL_Y + 52, 456, 70, RGB(0x1A, 0x24, 0x3A));
            draw_rect_frame_band(band_buf, by, bh, 12, PANEL_Y + 52, 456, 70, 2, rarity_color(nt.rarity));
            char ib[40];
            snprintf(ib, sizeof(ib), "NEW: %.10s HP%d A%d", nt.name, (int)nt.hp, (int)(nt.dmg + 0.5f));
            draw_text_shadow_band(band_buf, by, bh, 22, PANEL_Y + 62, ib, 2, C_WHITE);
            draw_text_band(band_buf, by, bh, 22, PANEL_Y + 90, "TAP SLOT TO REPLACE", 2, C_AQUA);

            for (int i = 0; i < ALLY_N; ++i) {
                int sx = SWAP_X[i], sy = SWAP_Y;
                fill_rect_band(band_buf, by, bh, sx, sy, SWAP_W, SWAP_H, RGB(0x1C, 0x1C, 0x2E));
                draw_rect_frame_band(band_buf, by, bh, sx, sy, SWAP_W, SWAP_H, 2, C_RED);
                if (game::st.party[i].active) {
                    const auto& ot = game::TALENTS[game::st.party[i].talent_id];
                    snprintf(ib, sizeof(ib), "SLOT %d", i + 1);
                    draw_text_band(band_buf, by, bh, sx + 10, sy + 12, ib, 2, C_AQUA);
                    char onm[12]; snprintf(onm, sizeof(onm), "%.10s", ot.name);
                    draw_text_shadow_band(band_buf, by, bh, sx + 10, sy + 40, onm, 2, rarity_color(ot.rarity));
                    snprintf(ib, sizeof(ib), "*%u %s", (unsigned)game::st.party[i].rank, ot.role_tag);
                    draw_text_band(band_buf, by, bh, sx + 10, sy + 68, ib, 2, C_WHITE);
                    snprintf(ib, sizeof(ib), "HP%d A%d", (int)ot.hp, (int)(ot.dmg + 0.5f));
                    draw_text_band(band_buf, by, bh, sx + 10, sy + 96, ib, 2, C_GOLD);
                    fill_rect_band(band_buf, by, bh, sx + 10, sy + 154, SWAP_W - 20, 46, RGB(0x70, 0x28, 0x28));
                    draw_rect_frame_band(band_buf, by, bh, sx + 10, sy + 154, SWAP_W - 20, 46, 2, C_GOLD);
                    draw_text_shadow_band(band_buf, by, bh, sx + (SWAP_W - text_w("SWAP", 2)) / 2, sy + 170, "SWAP", 2, C_WHITE);
                }
            }
        } else if (show_draft) {
            draw_text_shadow_band(band_buf, by, bh, 14, PANEL_Y + 12, "PICK 1", 3, C_GOLD);
            fill_rect_band(band_buf, by, bh, PEEKBTN_X, PEEKBTN_Y, PEEKBTN_W, PEEKBTN_H, RGB(0x20, 0x38, 0x54));
            draw_rect_frame_band(band_buf, by, bh, PEEKBTN_X, PEEKBTN_Y, PEEKBTN_W, PEEKBTN_H, 2, C_AQUA);
            draw_text_shadow_band(band_buf, by, bh, PEEKBTN_X + (PEEKBTN_W - text_w("STATUS", 2)) / 2, PEEKBTN_Y + 15, "STATUS", 2, C_WHITE);
            for (int c = 0; c < 3; ++c) draw_draft_card_band(band_buf, by, bh, c);
        } else {
            draw_text_shadow_band(band_buf, by, bh, 14, PANEL_Y + 12, "STATUS", 3, C_AQUA);
            if (game::st.draft_open) {
                fill_rect_band(band_buf, by, bh, PEEKBTN_X, PEEKBTN_Y, PEEKBTN_W, PEEKBTN_H, RGB(0x64, 0x3C, 0x10));
                draw_rect_frame_band(band_buf, by, bh, PEEKBTN_X, PEEKBTN_Y, PEEKBTN_W, PEEKBTN_H, 2, C_GOLD);
                draw_text_shadow_band(band_buf, by, bh, PEEKBTN_X + (PEEKBTN_W - text_w("DRAFT", 2)) / 2, PEEKBTN_Y + 15, "DRAFT", 2, C_WHITE);
            } else if (game::can_open_box()) {
                fill_rect_band(band_buf, by, bh, PEEKBTN_X, PEEKBTN_Y, PEEKBTN_W, PEEKBTN_H, RGB(0x64, 0x3C, 0x10));
                draw_rect_frame_band(band_buf, by, bh, PEEKBTN_X, PEEKBTN_Y, PEEKBTN_W, PEEKBTN_H, 2, C_GOLD);
                draw_text_shadow_band(band_buf, by, bh, PEEKBTN_X + (PEEKBTN_W - text_w("OPEN!", 2)) / 2, PEEKBTN_Y + 15, "OPEN!", 2, C_GOLD);
            }

            /* 3人スロットステータス (すべて 2x = 14px) */
            for (int i = 0; i < ALLY_N; ++i) {
                int cx = 12 + i * 155, cy = PANEL_Y + 52;
                fill_rect_band(band_buf, by, bh, cx, cy, 146, 108, RGB(0x18, 0x1E, 0x30));
                draw_rect_frame_band(band_buf, by, bh, cx, cy, 146, 108, 1, RGB(0x38, 0x48, 0x6A));
                if (game::st.party[i].active) {
                    const auto& td = game::TALENTS[game::st.party[i].talent_id];
                    char ln[16];
                    snprintf(ln, sizeof(ln), "%.10s", td.name);
                    draw_text_shadow_band(band_buf, by, bh, cx + 8, cy + 10, ln, 2, rarity_color(td.rarity));
                    snprintf(ln, sizeof(ln), "*%u %s", (unsigned)game::st.party[i].rank, td.role_tag);
                    draw_text_band(band_buf, by, bh, cx + 8, cy + 34, ln, 2, C_WHITE);
                    snprintf(ln, sizeof(ln), "HP %d", (int)allies[i].hp);
                    draw_text_band(band_buf, by, bh, cx + 8, cy + 60, ln, 2, C_MINT);
                    snprintf(ln, sizeof(ln), "MP %d%%", (int)allies[i].rage);
                    draw_text_band(band_buf, by, bh, cx + 8, cy + 84, ln, 2, C_AQUA);
                } else {
                    draw_text_band(band_buf, by, bh, cx + (146 - text_w("EMPTY", 2)) / 2, cy + 46, "EMPTY", 2, RGB(0x68, 0x74, 0x8C));
                }
            }

            /* ビルド合算バフ (2x 大きく見やすく) */
            char b1[32], b2[32];
            snprintf(b1, sizeof(b1), "ATK %d%%   HP %d%%", game::st.mods.dmg_pct, game::st.mods.hp_pct);
            snprintf(b2, sizeof(b2), "CRIT %d%%  VAMP %.1f", game::st.mods.crit_pct, game::st.mods.vamp_x10 / 10.0f);
            draw_text_band(band_buf, by, bh, 16, PANEL_Y + 172, b1, 2, C_GOLD);
            draw_text_band(band_buf, by, bh, 16, PANEL_Y + 196, b2, 2, C_MINT);

            /* 所持チャーム上位 4 件 (2列 x 2行, 2x バッジ) */
            int shown = 0;
            for (int cid = 0; cid < game::CHARM_N && shown < 4; ++cid) {
                uint8_t lv = game::st.charm_lv[cid];
                if (lv == 0) continue;
                int col = shown % 2, row = shown / 2;
                int bx = 12 + col * 232, b_y = PANEL_Y + 224 + row * 38;
                fill_rect_band(band_buf, by, bh, bx, b_y, 224, 32, RGB(0x1A, 0x24, 0x38));
                draw_rect_frame_band(band_buf, by, bh, bx, b_y, 224, 32, 1, rarity_color(game::CHARMS[cid].rarity));
                char cb[20];
                snprintf(cb, sizeof(cb), "%.12s LV%u", game::CHARMS[cid].name, (unsigned)lv);
                draw_text_band(band_buf, by, bh, bx + 8, b_y + 9, cb, 2, rarity_color(game::CHARMS[cid].rarity));
                shown++;
            }
            if (shown == 0) {
                draw_text_band(band_buf, by, bh, 16, PANEL_Y + 242, "NO CHARMS YET", 2, RGB(0x98, 0xA8, 0xC0));
            }

            char ftr[36];
            snprintf(ftr, sizeof(ftr), "RUN#%u BEST W%u CDX %d/64",
                     (unsigned)game::st.run_number, (unsigned)game::st.best_wave,
                     game::discovered_talent_count());
            draw_text_band(band_buf, by, bh, (FB_W - text_w(ftr, 2)) / 2, PANEL_Y + 322, ftr, 2, C_AQUA);
        }

        memcpy(fb + by * FB_W, band_buf, (size_t)(bh * FB_W * 2));
    }

    /* ドラフトプレビューで一時流用した敵スロットキャッシュを無効化 */
    if (show_draft) {
        for (int c = 0; c < 3; ++c) cached_atlas_id[ALLY_N + c] = -1;
    }
}

} // namespace beat
