/**
 * beat.cpp — オートバトルエンジン実装 (v3.1)
 *
 * 桜井スキル適用:
 * - モーション: 待機→構え(瞬時に極端に)→攻撃→フォロースルーは表示時間最長 (#モーション)
 * - 必殺技: ヒットストップ大+明暗交互フラッシュ+画面振動大+カットイン (#056/#221/#103)
 * - ヒットマーク7条件、ダメージ数字のカウントアップ気持ちよさ (#130/#198)
 * - 敵は一言で尖らせる (#204)、リトライ迅速 (#116)、パーティクルは寿命短く数絞る (#242)
 */
#include "beat.h"
#include "game_params.h"
#include "garden_game.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

namespace beat {

/* ---------------- 描画ヘルパ ---------------- */
static inline uint16_t RGB(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static const uint16_t C_SKY    = RGB(0x18, 0x1C, 0x38);
static const uint16_t C_FAR    = RGB(0x2A, 0x2C, 0x52);
static const uint16_t C_GRASS  = RGB(0x24, 0x3C, 0x34);
static const uint16_t C_GRASS2 = RGB(0x2A, 0x46, 0x3C);
static const uint16_t C_WHITE  = RGB(255, 255, 255);
static const uint16_t C_BLACK  = RGB(0x08, 0x08, 0x0C);
static const uint16_t C_HAIR   = RGB(0xC8, 0xD8, 0xE8);
static const uint16_t C_SKIN   = RGB(0xFF, 0xE4, 0xD0);
static const uint16_t C_DRESS  = RGB(0xE8, 0xF4, 0xFF);
static const uint16_t C_AQUA   = RGB(0x70, 0xD8, 0xE8);
static const uint16_t C_RED    = RGB(0xE0, 0x50, 0x50);

static void fill_rect(uint16_t* fb, int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > FB_W ? FB_W : x + w, y1 = y + h > FB_H ? FB_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t* row = fb + yy * FB_W;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}
static void fill_circle(uint16_t* fb, int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        fill_rect(fb, cx - dx, cy + dy, dx * 2 + 1, 1, c);
    }
}

static const uint8_t DIG[11][5] = {
    {0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7},{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x2,0x2,0x2},{0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7},{0x0,0x2,0x7,0x2,0x0}, /* '+' */
};
static void draw_digit(uint16_t* fb, int x, int y, int ch, int scale, uint16_t c)
{
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (DIG[ch][row] & (1 << (2 - col)))
                fill_rect(fb, x + col * scale, y + row * scale, scale, scale, c);
}
static void draw_number(uint16_t* fb, int x, int y, int value, int scale, uint16_t c)
{
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%d", value);
    for (const char* p = tmp; *p; p++) {
        int ch = (*p == '+') ? 10 : (*p - '0');
        if (ch < 0 || ch > 10) continue;
        draw_digit(fb, x, y, ch, scale, c);
        x += 4 * scale;
    }
}

/* 大文字英字 ('A'-'Z') + '-' + '!' */
static const uint8_t LET[26][5] = {
    {0x2,0x5,0x7,0x5,0x5},{0x6,0x5,0x6,0x5,0x6},{0x3,0x4,0x4,0x4,0x3},
    {0x6,0x5,0x5,0x5,0x6},{0x7,0x4,0x6,0x4,0x7},{0x7,0x4,0x6,0x4,0x4},
    {0x3,0x4,0x5,0x5,0x3},{0x5,0x5,0x7,0x5,0x5},{0x7,0x2,0x2,0x2,0x7},
    {0x1,0x1,0x1,0x5,0x2},{0x5,0x5,0x6,0x5,0x5},{0x4,0x4,0x4,0x4,0x7},
    {0x5,0x7,0x7,0x5,0x5},{0x6,0x5,0x5,0x5,0x5},{0x7,0x5,0x5,0x5,0x7},
    {0x6,0x5,0x6,0x4,0x4},{0x3,0x4,0x5,0x5,0x3},{0x6,0x5,0x6,0x5,0x5},
    {0x3,0x4,0x2,0x1,0x6},{0x7,0x2,0x2,0x2,0x2},{0x5,0x5,0x5,0x5,0x7},
    {0x5,0x5,0x5,0x5,0x2},{0x5,0x5,0x7,0x7,0x5},{0x5,0x5,0x2,0x5,0x5},
    {0x5,0x5,0x2,0x2,0x2},{0x7,0x1,0x2,0x4,0x7},
};
static const uint8_t GL_DASH[5] = {0x0,0x0,0x7,0x0,0x0};
static const uint8_t GL_BANG[5] = {0x2,0x2,0x2,0x0,0x2};
static const uint8_t GL_SPC[5]  = {0x0,0x0,0x0,0x0,0x0};

static const uint8_t* glyph(char c)
{
    if (c >= '0' && c <= '9') return DIG[c - '0'];
    if (c == '+') return DIG[10];
    if (c >= 'A' && c <= 'Z') return LET[c - 'A'];
    if (c == '-') return GL_DASH;
    if (c == '!') return GL_BANG;
    return GL_SPC;
}
static int draw_char(uint16_t* fb, int x, int y, char c, int scale, uint16_t col)
{
    const uint8_t* g = glyph(c);
    for (int row = 0; row < 5; row++)
        for (int col2 = 0; col2 < 3; col2++)
            if (g[row] & (1 << (2 - col2)))
                fill_rect(fb, x + col2 * scale, y + row * scale, scale, scale, col);
    return 4 * scale;
}
static int draw_text(uint16_t* fb, int x, int y, const char* s, int scale, uint16_t col)
{
    for (const char* p = s; *p; p++) x += draw_char(fb, x, y, *p, scale, col);
    return x;
}
static int text_w(const char* s, int scale) { return (int)strlen(s) * 4 * scale; }

/* ---------------- アクタ ---------------- */
enum AState : uint8_t { A_IDLE, A_WALK, A_WINDUP, A_RECOVER, A_HURT, A_DOWN, A_SPECIAL };
enum EType : uint8_t { E_SLIME, E_BAT, E_GOLEM, E_BOSS };

struct Actor {
    bool alive = false;
    bool ally = false;
    uint8_t type = 0;            /* ally: 0=LUMI 1=MIRA 2=GORO / enemy: E_* */
    float x = 0, y = 0;
    int8_t dir = 1;
    uint8_t state = A_IDLE;
    float hp = 10, hpmax = 10;
    float vx = 0;
    uint16_t timer = 0;
    uint8_t combo = 0;
    uint8_t flash = 0;
    bool invuln = false;
    uint16_t invuln_t = 0;
    float rage = 0;              /* 必殺技ゲージ 0..100 (味方のみ) */
    float home_x = 0;
    float wander_t = 0;
    uint8_t spawn_t = 0;         /* 出現演出中 (点滅, 行動停止) */
    int16_t px = -9999, py = -9999;   /* 前フレーム描画位置 (ダーティ矩形) */
};

static Actor allies[ALLY_N];
static Actor enemies[ENEMY_N];

/* 背景スナップショット (ダーティ矩形復元用) */
static uint16_t* bg = nullptr;
static bool full_restore_pending = false;
static uint32_t wave_n = 1;
static uint16_t wave_timer = 60;
static bool wave_active = false;
static uint8_t freeze = 0;
static uint8_t special_flash = 0;
static uint8_t shake = 0, shake_mag = 0;
static uint32_t rngs = 13371;
static char cutin_name[24];
static uint16_t cutin_draw = 0;        /* カットイン表示残りフレーム */
static char flash_msg_buf[24];
static uint16_t flash_frames = 0;      /* 画面内メッセージ残りフレーム */
static uint32_t frame = 0;

struct Fx {
    bool on;
    float x, y, vx;
    int8_t kind;      /* 0=数字 1=ドロップ 2=ヒットマーク 3=出現! 4=弾 5=必殺残光 */
    int16_t life;
    int32_t v;        /* 数字の値 or ダメージ or target index */
    float dmg;
    int8_t src;       /* 弾の発射者 (rage 用) */
    int16_t px = -9999, py = -9999;
};
static Fx fxs[20];
static uint8_t fx_i = 0;

/* 味方スペック (攻撃モーションのバリエーション) */
struct AllyDef {
    const char* name;
    const char* special;
    float hp, spd, reach, windup, recover, dmg;
    uint8_t atktype;   /* 0=近接コンボ 1=遠距離弾 2=重打撃AoE */
};
static const AllyDef ADEF[ALLY_N] = {
    /* LUMI */ {"LUMI", "CRYSTAL SHOWER", 100, 1.1f, 40,  8, 10, 10, 0},
    /* MIRA */ {"MIRA", "AQUA STORM",     70, 0.8f, 150, 18, 14, 6,  1},
    /* GORO */ {"GORO", "GRAND SLAM",    150, 0.5f, 36,  26, 20, 18, 2},
};

static inline uint32_t rnd() { rngs ^= rngs << 13; rngs ^= rngs >> 17; rngs ^= rngs << 5; return rngs; }
static inline float frand(float a, float b) { return a + (rnd() % 1000) / 1000.0f * (b - a); }

static void add_fx(float x, float y, int8_t kind, int32_t v, int16_t life)
{
    /* 空きスロットを探す (生きている FX を上書きしてワープさせない) */
    Fx* slot = nullptr;
    for (auto& f : fxs) if (!f.on) { slot = &f; break; }
    if (!slot) { slot = &fxs[fx_i]; fx_i = (fx_i + 1) % 20; }
    *slot = Fx();
    slot->on = true;
    slot->x = x; slot->y = y;
    slot->kind = kind; slot->life = life; slot->v = v;
}
static void add_shake(uint8_t mag, uint8_t frames)
{
    if (mag >= shake_mag) { shake_mag = mag; shake = frames; }
}

/* ---------------- 背景スナップショット ---------------- */
static void draw_bg_to(uint16_t* t)
{
    fill_rect(t, 0, 0, FB_W, FB_H, C_SKY);
    for (int i = 0; i < 10; i++) {
        int sx = (i * 240) % (FB_W + 240) - 120;
        fill_rect(t, sx, 60, 36, 120, C_FAR);
        fill_rect(t, sx - 8, 90, 52, 8, C_FAR);
    }
    fill_rect(t, 0, (int)BELT_Y0, FB_W, (int)(BELT_Y1 - BELT_Y0) + 10, C_GRASS);
    for (int i = 0; i < 30; i++) {
        int gx = i * 64;
        if (gx > -64 && gx < FB_W) fill_rect(t, gx, (int)BELT_Y0 + 20, 24, 2, C_GRASS2);
    }
}

/* 背景スナップショットから矩形を復元 */
static void restore_rect(uint16_t* fb, int x, int y, int w, int h)
{
    if (!bg) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > FB_W ? FB_W : x + w, y1 = y + h > FB_H ? FB_H : y + h;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; yy++)
        memcpy(fb + yy * FB_W + x0, bg + yy * FB_W + x0, (x1 - x0) * 2);
}

/* ---------------- 初期化 ---------------- */
void init()
{
    for (int i = 0; i < ALLY_N; i++) {
        Actor& a = allies[i];
        a = Actor();
        a.alive = true; a.ally = true; a.type = i;
        a.hpmax = ADEF[i].hp * gp::g.hp_pct / 100.0f;
        a.hp = a.hpmax;
        a.x = 40 + i * 34;
        a.y = (i == 0) ? 220 : (i == 1 ? 170 : 280);
        a.home_x = a.x;
        a.dir = 1;
    }
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    if (!bg) bg = (uint16_t*)heap_caps_calloc(FB_W * FB_H, 2, MALLOC_CAP_SPIRAM);
    draw_bg_to(bg);
    full_restore_pending = true;   /* 初回は全体復元 */
    wave_n = 1; wave_timer = 60; wave_active = false;
    freeze = 0; special_flash = 0; shake = 0; shake_mag = 0;
    cutin_draw = 0; flash_frames = 0;
}

void refresh_player()
{
    for (auto& a : allies) {
        float nm = ADEF[a.type].hp * gp::g.hp_pct / 100.0f;
        if (nm != a.hpmax) { a.hp = a.hp * nm / a.hpmax; a.hpmax = nm; }
    }
}
void set_wave(uint32_t w) { wave_n = w ? w : 1; wave_timer = 40; wave_active = false; }
uint32_t wave() { return wave_n; }
void heal_all() { for (auto& a : allies) { a.hp = a.hpmax; } }
float ally_hp(int i) { return allies[i].hp; }
float ally_hpmax(int i) { return allies[i].hpmax; }
float ally_rage(int i) { return allies[i].rage; }

void flash_message(const char* msg, uint16_t frames)
{
    snprintf(flash_msg_buf, sizeof(flash_msg_buf), "%s", msg);
    flash_frames = frames;
}

void request_full_restore() { full_restore_pending = true; }

void dump()
{
    Serial.println("[dump] --- allies ---");
    for (auto& a : allies)
        Serial.printf("  %-5s x=%.0f y=%.0f hp=%.0f/%.0f st=%u rage=%.0f\r\n",
                      ADEF[a.type].name, a.x, a.y, a.hp, a.hpmax, a.state, a.rage);
    Serial.println("[dump] --- enemies ---");
    for (auto& e : enemies) {
        if (!e.alive) continue;
        Serial.printf("  type%u x=%.0f y=%.0f hp=%.0f st=%u spawn=%u px=%d\r\n",
                      e.type, e.x, e.y, e.hp, e.state, e.spawn_t, e.px);
    }
}

/* ---------------- 応答 (take_cutin は廃止: fb 直接描画) ---------------- */

/* ---------------- スポーン ---------------- */
static Actor* free_enemy()
{
    for (auto& e : enemies) if (!e.alive) return &e;
    return nullptr;
}
static void spawn_enemy(int type)
{
    Actor* e = free_enemy();
    if (!e) return;
    float hpbase;
    switch (type) {
    case E_BAT:   hpbase = 12; break;
    case E_GOLEM: hpbase = 60; break;
    case E_BOSS:  hpbase = 200; break;
    default:      hpbase = 20; break;
    }
    float hpmax = hpbase + wave_n * (type == E_BOSS ? 30 : 6);
    *e = Actor();
    e->alive = true; e->type = (uint8_t)type;
    e->x = FB_W + 20 + frand(0, 60);
    e->y = frand(BELT_Y0 + 10, BELT_Y1 - 10);
    e->hpmax = hpmax; e->hp = hpmax;
    e->dir = -1; e->state = A_WALK;
    e->spawn_t = 24;                 /* 出現演出 (点滅しながら歩いて入場) */
    if (type == E_BOSS) add_fx(e->x - 30, e->y - 44, 3, 0, 90);
}

static void start_wave()
{
    wave_active = true;
    bool boss = (wave_n % 5 == 0);
    int n = 3 + (int)(wave_n / 2);
    if (n > 6) n = 6;
    if (boss) { spawn_enemy(E_BOSS); n = 2; }
    for (int i = 0; i < n; i++) {
        int t = E_SLIME;
        uint32_t r = rnd() % 100;
        if (wave_n >= 2 && r < 30) t = E_BAT;
        if (wave_n >= 3 && r >= 30 && r < 50) t = E_GOLEM;
        spawn_enemy(t);
    }
}

/* ---------------- ダメージ処理 ---------------- */
static void rage_add(Actor& a, float v)
{
    a.rage += v * gp::g.rage_pct / 100.0f;
    if (a.rage > 100) a.rage = 100;
}

static void hit_enemy(Actor& e, float dmg, int8_t dir, bool big, int src_ally)
{
    e.hp -= dmg;
    e.flash = 3;
    e.state = A_HURT; e.timer = 14;
    e.vx = dir * (big ? 5.0f : 2.6f);
    add_fx(e.x, e.y - 24, 2, 0, 3);
    add_fx(e.x + frand(-6, 6), e.y - 36, 0, (int32_t)dmg, 30);
    freeze = big ? 6 : 4;
    if (src_ally >= 0) rage_add(allies[src_ally], 7);
    if (e.hp <= 0) {
        e.alive = false;
        double drop = (e.type == E_BOSS ? 200 : 10) + wave_n * 2;
        game::add_drops(drop);
        add_fx(e.x, e.y - 28, 1, (int32_t)drop, 36);
        add_shake(e.type == E_BOSS ? 4 : 1, e.type == E_BOSS ? 20 : 5);
    }
}

static void hurt_ally(Actor& a, float dmg, int8_t dir)
{
    if (a.invuln || a.state == A_DOWN) return;
    a.hp -= dmg;
    a.flash = 3;
    a.state = A_HURT; a.timer = 14;
    a.vx = dir * 2.2f;
    rage_add(a, 12);
    add_fx(a.x, a.y - 36, 2, 0, 3);
    add_fx(a.x, a.y - 44, 0, -(int32_t)dmg, 30);
    freeze = 3;
    if (a.hp <= 0) { a.hp = 0; a.state = A_DOWN; a.timer = 90; add_shake(2, 8); }
}

/* ---------------- 必殺技 (条件発動) ---------------- */
static void try_special(Actor& a)
{
    if (a.rage < 100) return;
    if (a.state != A_IDLE && a.state != A_WALK) return;

    a.rage = 0;
    a.state = A_SPECIAL;
    a.timer = 40;
    freeze = 8;
    special_flash = 6;
    add_shake(4, 18);
    snprintf(cutin_name, sizeof(cutin_name), "%s - %s", ADEF[a.type].name, ADEF[a.type].special);
    cutin_draw = 50;

    const AllyDef& d = ADEF[a.type];
    float dmg = d.dmg * gp::g.dmg_pct / 100.0f;
    for (auto& e : enemies) {
        if (!e.alive) continue;
        if (a.type == 2 && (e.x - a.x) > 140) continue;   /* GORO は前方範囲 */
        hit_enemy(e, dmg * (e.type == E_BOSS ? 1.0f : 1.0f) * (a.type == 0 ? 4 : (a.type == 1 ? 3 : 4)),
                  (e.x > a.x) ? 1 : -1, true, a.type);
    }
}

/* ---------------- 味方 AI ---------------- */
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

static void ally_step(Actor& a)
{
    const AllyDef& d = ADEF[a.type];

    if (a.state == A_DOWN) {
        if (--a.timer == 0) {           /* 復帰は迅速に (#116) */
            a.state = A_IDLE;
            a.hp = a.hpmax;
            a.invuln = true; a.invuln_t = 60;
            a.x = a.home_x;
        }
        return;
    }
    if (a.invuln && --a.invuln_t == 0) a.invuln = false;
    if (a.state == A_HURT) {
        if (--a.timer == 0) a.state = A_IDLE;
        a.x += a.vx; a.vx *= 0.85f;
        return;
    }
    if (a.state == A_SPECIAL) {
        if (--a.timer == 0) a.state = A_IDLE;
        return;
    }
    if (a.state == A_WINDUP) {
        if (--a.timer == 0) {
            /* STRIKE (1F で一気に) */
            Actor* t = nearest_enemy(a.x, a.y);
            if (d.atktype == 0 && t) {                       /* LUMI: 3 段コンボ */
                uint8_t st = a.combo;
                float dmg = d.dmg * gp::g.dmg_pct / 100.0f * (st == 2 ? 2.0f : 1.0f);
                float dx = t->x - a.x;
                if (t->alive && fabsf(dx) < d.reach && fabsf(t->y - a.y) < 18) {
                    hit_enemy(*t, dmg, dx > 0 ? 1 : -1, st == 2, a.type);
                    if (st == 2) add_shake(1, 3);
                }
                a.combo = (st + 1) % 3;
            } else if (d.atktype == 1 && t) {                /* MIRA: 追尾弾 */
                Fx* slot = nullptr;
                for (auto& f2 : fxs) if (!f2.on) { slot = &f2; break; }
                if (!slot) { slot = &fxs[fx_i]; fx_i = (fx_i + 1) % 20; }
                *slot = Fx();
                slot->on = true;
                slot->x = a.x + a.dir * 10; slot->y = a.y - 20;
                slot->vx = (t->x > a.x ? 1 : -1) * 5.0f;
                slot->kind = 4; slot->life = 60;
                slot->dmg = d.dmg * gp::g.dmg_pct / 100.0f;
                slot->src = (int8_t)a.type;
                slot->px = -9999; slot->py = -9999;
                for (int i = 0; i < ENEMY_N; i++) if (&enemies[i] == t) { slot->v = i; break; }
            } else if (d.atktype == 2) {                     /* GORO: 前方 AoE スマッシュ */
                for (auto& e : enemies) {
                    if (!e.alive) continue;
                    float dx = e.x - a.x;
                    if (dx * a.dir > -10 && fabsf(dx) < d.reach + 20 && fabsf(e.y - a.y) < 22) {
                        hit_enemy(e, d.dmg * gp::g.dmg_pct / 100.0f, a.dir, true, a.type);
                    }
                }
                add_shake(2, 6);
            }
            a.state = A_RECOVER;                             /* フォロースルーは長め */
            a.timer = (uint16_t)d.recover;
        }
        return;
    }
    if (a.state == A_RECOVER) {
        if (--a.timer == 0) a.state = A_IDLE;
        return;
    }

    /* IDLE / WALK: 目標へ移動して攻撃 */
    Actor* t = nearest_enemy(a.x, a.y);
    if (!t) {
        /* わちゃわちゃ待機: ホーム付近をうろうろ */
        a.wander_t += 1;
        float wob = sinf(a.wander_t * 0.05f + a.home_x) * 14;
        float tx = a.home_x + wob;
        if (fabsf(tx - a.x) > 1) {
            a.x += (tx > a.x ? 1 : -1) * d.spd * 0.4f;
            a.dir = tx > a.x ? 1 : -1;
            a.state = A_WALK;
        } else a.state = A_IDLE;
        rage_add(a, 0.04f);                                  /* 待機中もゆっくり充填 */
        return;
    }
    float dx = t->x - a.x, dy = t->y - a.y;
    float reach = (d.atktype == 1) ? d.reach : d.reach;
    if (fabsf(dx) > reach) {
        a.dir = dx > 0 ? 1 : -1;
        a.x += a.dir * d.spd;
        a.y += (dy > 0 ? 1 : -1) * d.spd * 0.5f;
        if (a.y < BELT_Y0) a.y = BELT_Y0;
        if (a.y > BELT_Y1) a.y = BELT_Y1;
        a.state = A_WALK;
    } else {
        a.dir = dx > 0 ? 1 : -1;
        a.state = A_WINDUP;                                  /* 構えは瞬間ポーズで */
        a.timer = (uint16_t)d.windup;
    }
    /* 積極的にゲージを溜めて必殺技へ */
    try_special(a);
}

/* ---------------- 敵 AI ---------------- */
static Actor* nearest_ally(float x, float y)
{
    Actor* best = nullptr;
    float bd = 1e9f;
    for (auto& a : allies) {
        if (!a.alive || a.state == A_DOWN) continue;
        float d = fabsf(a.x - x) + fabsf(a.y - y) * 0.5f;
        if (d < bd) { bd = d; best = &a; }
    }
    return best;
}

static void enemy_step(Actor& e)
{
    if (e.spawn_t) { e.spawn_t--; return; }   /* 出現演出中は行動しない */
    if (e.state == A_HURT) {
        if (--e.timer == 0) e.state = A_WALK;
        e.x += e.vx; e.vx *= 0.85f;
        return;
    }
    if (e.state == A_WINDUP) {
        if (--e.timer == 0) {
            Actor* t = nearest_ally(e.x, e.y);
            float reach = (e.type == E_GOLEM || e.type == E_BOSS) ? 40 : 28;
            if (t) {
                float dx = t->x - e.x, dy = t->y - e.y;
                if (fabsf(dx) < reach + 6 && fabsf(dy) < 16) {
                    float dmg = (e.type == E_BOSS) ? 14 : (e.type == E_GOLEM ? 10 : 5);
                    hurt_ally(*t, dmg, dx > 0 ? 1 : -1);
                }
            }
            e.state = A_RECOVER; e.timer = 30;
        }
        return;
    }
    if (e.state == A_RECOVER) {
        if (--e.timer == 0) e.state = A_WALK;
        return;
    }
    /* WALK */
    Actor* t = nearest_ally(e.x, e.y);
    if (!t) { e.state = A_IDLE; return; }
    float dx = t->x - e.x, dy = t->y - e.y;
    float reach = (e.type == E_GOLEM || e.type == E_BOSS) ? 36 : 26;
    float sp = (e.type == E_BAT) ? 1.1f : (e.type == E_GOLEM ? 0.4f : (e.type == E_BOSS ? 0.5f : 0.6f));
    if (fabsf(dx) > reach) {
        e.dir = dx > 0 ? 1 : -1;
        e.x += e.dir * sp;
        if (e.x < 10) e.x = 10;
        e.y += (dy > 0 ? 1 : -1) * sp * 0.5f;
        if (e.y < BELT_Y0) e.y = BELT_Y0;
        if (e.y > BELT_Y1) e.y = BELT_Y1;
    } else {
        e.dir = dx > 0 ? 1 : -1;
        e.state = A_WINDUP; e.timer = 24;
    }
}

/* ---------------- 弾・FX ---------------- */
static void fx_step()
{
    for (auto& f : fxs) {
        if (!f.on) continue;
        if (f.kind == 0 || f.kind == 1) f.y -= 0.6f;
        if (f.kind == 4) {
            /* 追尾弾: 目標へ飛ぶ */
            f.x += f.vx;
            if (f.life % 3 == 0) add_fx(f.x, f.y + frand(-4, 4), 5, 0, 4); /* 残光 */
            Actor& t = enemies[f.v];
            if (!t.alive || f.life <= 1 || (t.x - f.x) * f.vx < 0) {
                if (t.alive) hit_enemy(t, f.dmg, f.vx > 0 ? 1 : -1, false, f.src);
                f.on = false;
                continue;
            }
        }
        if (--f.life <= 0) f.on = false;
    }
}

void step()
{
    frame++;
    if (freeze > 0) { freeze--; return; }         /* ヒットストップ中 */
    if (special_flash > 0) special_flash--;

    for (auto& a : allies) if (a.alive) ally_step(a);
    for (auto& e : enemies) if (e.alive) enemy_step(e);
    fx_step();

    int alive = 0;
    for (auto& e : enemies) if (e.alive) alive++;
    if (wave_active) {
        if (alive == 0) { wave_active = false; wave_n++; wave_timer = 180; }
    } else if (wave_timer > 0) wave_timer--;
    else start_wave();

    if (shake > 0) shake--;
    if (flash_frames) flash_frames--;

    /* 安全網: 定期的に背景全体を復元 (LVGL 側の予期せぬ上書きを自己修復) */
    static uint16_t heal = 0;
    if (++heal >= 120) { heal = 0; full_restore_pending = true; }
}

/* ---------------- 描画 ---------------- */
static uint32_t blink_tick = 0;
static bool blink_on() { return ((blink_tick++) / 4) % 2 == 0; }

static void draw_shadow(uint16_t* fb, int sx, int sy, int w)
{
    fill_rect(fb, sx - w / 2, sy - 2, w, 3, RGB(0x14, 0x22, 0x1C));
}

/* 味方ちびプレースホルダ (P3' でスプライト差し替え) */
static void draw_ally(uint16_t* fb, Actor& a, int sx, int sy)
{
    uint16_t hair = a.flash ? C_WHITE : C_HAIR;
    uint16_t body = a.flash ? C_WHITE : C_DRESS;
    int d = a.dir;
    if (a.state == A_DOWN) {
        draw_shadow(fb, sx, sy, 26);
        fill_rect(fb, sx - 12, sy - 10, 24, 8, a.flash ? C_WHITE : body);
        fill_circle(fb, sx + d * 8, sy - 10, 6, hair);
        return;
    }
    draw_shadow(fb, sx, sy, 24);
    /* 脚 */
    fill_rect(fb, sx - 6, sy - 8, 4, 8, a.flash ? C_WHITE : RGB(0xF0, 0xE0, 0xC0));
    fill_rect(fb, sx + 2, sy - 8, 4, 8, a.flash ? C_WHITE : RGB(0xF0, 0xE0, 0xC0));
    if (a.type == 2) {
        /* GORO: 石の体 */
        uint16_t c = a.flash ? C_WHITE : RGB(0x8A, 0x8A, 0x78);
        fill_rect(fb, sx - 12, sy - 26, 24, 20, c);
        fill_circle(fb, sx, sy - 30, 7, a.flash ? C_WHITE : RGB(0x9A, 0x9A, 0x88));
        if (a.state == A_WINDUP && blink_on())
            fill_rect(fb, sx + d * 10, sy - 22, 12, 6, C_WHITE);
        else
            fill_rect(fb, sx + (d > 0 ? 10 : -16), sy - 22, 6, 6, c);
    } else {
        /* ワンピ (LUMI) / ローブ (MIRA) */
        uint16_t robe = a.flash ? C_WHITE : (a.type == 1 ? C_AQUA : body);
        for (int i = 0; i < 14; i++)
            fill_rect(fb, sx - (8 + i / 3), sy - 8 - i, (16 + (i / 3) * 2), 1, robe);
        fill_circle(fb, sx, sy - 28, 9, hair);
        fill_circle(fb, sx + d * 3, sy - 26, 5, a.flash ? C_WHITE : C_SKIN);
        fill_rect(fb, sx + d * 4, sy - 27, 2, 2, C_AQUA);
        fill_circle(fb, sx - d * 8, sy - 26, 4, hair);
        /* 腕・モーション */
        if (a.state == A_WINDUP) {                    /* 構え: 引いた姿勢 (瞬時に極端に) */
            fill_rect(fb, sx - d * 10, sy - 20, 8, 4, a.flash ? C_WHITE : C_SKIN);
        } else if (a.state == A_SPECIAL) {
            fill_rect(fb, sx + d * 8, sy - 24, 14, 4, C_WHITE);
            fill_circle(fb, sx + d * 26, sy - 22, 6, C_AQUA);
        } else if (a.type == 1 && a.state == A_WALK) {
            fill_rect(fb, sx + d * 8, sy - 22, 3, 14, RGB(0xC0, 0x90, 0x50)); /* 杖 */
            fill_circle(fb, sx + d * 10, sy - 24, 3, C_AQUA);
        } else {
            fill_rect(fb, sx + (d > 0 ? 6 : -9), sy - 18, 4, 8, a.flash ? C_WHITE : C_SKIN);
        }
    }
}

static void draw_enemy(uint16_t* fb, Actor& e, int sx, int sy)
{
    if (e.spawn_t && blink_on()) return;   /* 出現演出: 点滅しながら実体化 */
    uint16_t c;
    int w, h;
    switch (e.type) {
    case E_BAT:   c = RGB(0x9C, 0x6C, 0xD8); w = 18; h = 14; break;
    case E_GOLEM: c = RGB(0x8A, 0x8A, 0x78); w = 26; h = 30; break;
    case E_BOSS:  c = RGB(0xD0, 0x40, 0x60); w = 40; h = 44; break;
    default:      c = RGB(0x50, 0xC0, 0x60); w = 20; h = 16; break;
    }
    if (e.flash) c = C_WHITE;
    if (e.state == A_WINDUP && blink_on()) c = C_WHITE;
    draw_shadow(fb, sx, sy, w);
    fill_circle(fb, sx, sy - h / 2 - 2, h / 2, c);
    uint16_t eye = RGB(0x20, 0x20, 0x28);
    int d = (allies[0].x > e.x) ? 1 : -1;
    if (!e.flash) {
        fill_rect(fb, sx + d * 2 - 1, sy - h / 2 - 4, 3, 3, eye);
        fill_rect(fb, sx + d * 6 - 1, sy - h / 2 - 4, 3, 3, eye);
    }
    if (e.hp < e.hpmax) {
        int bw = w + 8;
        fill_rect(fb, sx - bw / 2, sy - h - 10, bw, 3, C_BLACK);
        fill_rect(fb, sx - bw / 2 + 1, sy - h - 9, (int)((bw - 2) * e.hp / e.hpmax), 1, C_RED);
    }
}

static void draw_fx(uint16_t* fb, Fx& f)
{
    int sx = (int)f.x, sy = (int)f.y;
    switch (f.kind) {
    case 0: draw_number(fb, sx, sy, f.v, 2, C_WHITE); break;
    case 1:
        draw_digit(fb, sx - 6, sy, 10, 2, C_AQUA);
        draw_number(fb, sx + 8, sy, f.v, 1, C_AQUA);
        break;
    case 2:
        fill_circle(fb, sx, sy, 2, C_WHITE);
        for (int i = 0; i < 4; i++) {
            int dx = (i == 0) - (i == 1), dy = (i == 2) - (i == 3);
            fill_rect(fb, sx + dx * 5, sy + dy * 5, dx ? 5 : 2, dy ? 5 : 2, C_BLACK);
            fill_rect(fb, sx + dx * 5 + (dx < 0 ? 1 : 0), sy + dy * 5 + (dy < 0 ? 1 : 0), dx ? 4 : 1, dy ? 4 : 1, C_WHITE);
        }
        break;
    case 3:
        fill_rect(fb, sx - 2, sy, 5, 12, C_RED);
        fill_rect(fb, sx - 2, sy + 16, 5, 5, C_RED);
        break;
    case 4: fill_circle(fb, sx, sy, 4, C_AQUA); fill_circle(fb, sx, sy, 2, C_WHITE); break;
    case 5: fill_rect(fb, sx, sy, 3, 3, C_AQUA); break;
    }
}

void render(uint16_t* fb)
{
    /* 必殺技フラッシュ: 全画面を明暗交互に塗ってから、次フレームで全体復元 */
    if (special_flash > 0) {
        fill_rect(fb, 0, 0, FB_W, FB_H, (special_flash & 1) ? C_WHITE : C_BLACK);
        full_restore_pending = true;
        return;
    }
    if (full_restore_pending && bg) {
        memcpy(fb, bg, FB_W * FB_H * 2);
        full_restore_pending = false;
    }

    /* 消滅したアクタ・FX の前フレーム跡地を掃除 (揺れマージン +8px) */
    for (auto& e : enemies) {
        if (!e.alive && e.px != -9999) {
            restore_rect(fb, e.px - 32, e.py - 72, 64, 80);
            e.px = -9999;
        }
    }
    for (auto& f : fxs) {
        if (!f.on && f.px != -9999) {
            restore_rect(fb, f.px - 22, f.py - 26, 44, 56);
            f.px = -9999;
        }
    }
    /* 移動したアクタの前フレームを復元 */
    for (auto& a : allies) {
        if (a.px != -9999) restore_rect(fb, a.px - 42, a.py - 72, 84, 84);
    }
    for (auto& e : enemies) {
        if (e.alive && e.px != -9999) restore_rect(fb, e.px - 32, e.py - 72, 64, 80);
    }
    for (auto& f : fxs) {
        if (f.on && f.px != -9999) restore_rect(fb, f.px - 22, f.py - 26, 44, 56);
    }

    /* 描画 (y ソート, 画面振動オフセット付き) */
    int ox = 0, oy = 0;
    if (shake > 0) {
        oy = (rnd() % (shake_mag * 2 + 1)) - shake_mag;
        if (shake_mag > 2) ox = (rnd() % (shake_mag * 2 + 1)) - shake_mag;
    }

    Actor* order[ALLY_N + ENEMY_N];
    int n = 0;
    for (auto& a : allies) if (a.alive) order[n++] = &a;
    for (auto& e : enemies) if (e.alive) order[n++] = &e;
    for (int i = 1; i < n; i++) {
        Actor* k = order[i];
        int j = i - 1;
        while (j >= 0 && order[j]->y > k->y) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    for (int i = 0; i < n; i++) {
        Actor* a = order[i];
        int sx = (int)a->x + ox, sy = (int)a->y + oy;
        if (a->ally) draw_ally(fb, *a, sx, sy); else draw_enemy(fb, *a, sx, sy);
        a->px = (int16_t)a->x; a->py = (int16_t)a->y;
    }

    for (auto& f : fxs) if (f.on) {
        int sx = (int)f.x + ox, sy = (int)f.y + oy;
        float wx = f.x;
        float wy = f.y;
        f.x = sx; f.y = sy;
        draw_fx(fb, f);
        f.x = wx; f.y = wy;
        f.px = (int16_t)f.x; f.py = (int16_t)f.y;
    }

    /* 必殺技カットイン (fb に直接描画。LVGL は介在しない) */
    if (cutin_draw > 0) {
        uint16_t band = RGB(0x1C, 0x2A, 0x50);
        fill_rect(fb, 0, 96, FB_W, 30, band);
        fill_rect(fb, 0, 226, FB_W, 30, band);
        fill_rect(fb, 0, 96, FB_W, 2, C_WHITE);
        fill_rect(fb, 0, 254, FB_W, 2, C_WHITE);
        int w = text_w(cutin_name, 3);
        draw_text(fb, (FB_W - w) / 2, 148, cutin_name, 3, C_WHITE);
        cutin_draw--;
    }

    /* 画面内メッセージ */
    if (flash_frames > 0 && flash_msg_buf[0]) {
        int w = text_w(flash_msg_buf, 2);
        draw_text(fb, (FB_W - w) / 2, 66, flash_msg_buf, 2, RGB(0xFF, 0x9A, 0xB8));
    }
}

} // namespace beat
