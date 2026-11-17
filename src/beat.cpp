/**
 * beat.cpp — ベルトスクロール戦闘エンジン実装
 * 桜井スキル適用: ヒットストップ(#130/specs), ヒットマーク7条件(#130),
 * 画面振動パターン(#103), 明暗フラッシュ(#221), リトライ迅速(#116), パーティクル節約(#242)
 */
#include "beat.h"
#include "game_params.h"
#include "garden_game.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

namespace beat {

/* -------------------------------------------------- 基本描画ヘルパ */
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
/* 3x5 ミニフォント ('0'-'9', '+') */
static const uint8_t DIG[11][5] = {
    {0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7},{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x2,0x2,0x2},{0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7},{0x0,0x2,0x7,0x2,0x0}, /* '+' = index 10 */
};
static void draw_digit(uint16_t* fb, int x, int y, int ch, int scale, uint16_t c)
{
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (DIG[ch][row] & (1 << (2 - col)))
                fill_rect(fb, x + col * scale, y + row * scale, scale, scale, c);
}
static int draw_number(uint16_t* fb, int x, int y, int value, int scale, uint16_t c)
{
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%d", value);
    for (const char* p = tmp; *p; p++) {
        int ch = (*p == '+') ? 10 : (*p - '0');
        if (ch < 0 || ch > 10) continue;
        draw_digit(fb, x, y, ch, scale, c);
        x += 4 * scale;
    }
    return x;
}

/* -------------------------------------------------- 状態 */
enum AState : uint8_t { A_IDLE, A_WALK, A_ATK, A_WINDUP, A_RECOVER, A_HURT, A_DOWN };
enum EType : uint8_t { E_SLIME, E_BAT, E_GOLEM, E_BOSS };

struct Actor {
    bool alive = false;
    bool is_player = false;
    uint8_t type = 0;
    float x = 0, y = 0;          /* x: world, y: belt (canvas 座標) */
    int8_t dir = 1;
    uint8_t state = A_IDLE;
    float hp = 10, hpmax = 10;
    float vx = 0;                /* ノックバック速度 */
    uint16_t timer = 0;          /* 状態タイマ */
    uint8_t combo = 0;           /* プレイヤー: 次のコンボ段 */
    uint8_t flash = 0;           /* 白フラッシュ残り */
    bool invuln = false;
};

static Actor player;
static Actor enemies[8];
static float cam_x = 0;
static uint32_t wave_n = 1;
static int spawn_left = 0;
static uint16_t wave_timer = 0;
static bool wave_active = false;
static uint8_t freeze = 0;       /* ヒットストップ */
static uint8_t shake = 0;        /* 画面振動残り */
static uint8_t shake_mag = 0;
static uint32_t rngs = 99991;
static uint32_t drops_pending = 0;

struct Fx { bool on; float x, y; int8_t kind; int16_t life; int32_t v; };
static Fx fxs[16];
static uint8_t fx_i = 0;

Pad pad;

/* -------------------------------------------------- 小ヘルパ */
static inline uint32_t rnd() { rngs ^= rngs << 13; rngs ^= rngs >> 17; rngs ^= rngs << 5; return rngs; }
static inline float frand(float a, float b) { return a + (rnd() % 1000) / 1000.0f * (b - a); }

static void add_fx(float x, float y, int8_t kind, int32_t v, int16_t life)
{
    Fx& f = fxs[fx_i];
    fx_i = (fx_i + 1) & 15;
    f = { true, x, y, kind, life, v };
}
static void shake_add(uint8_t mag, uint8_t frames)
{
    if (mag >= shake_mag) { shake_mag = mag; shake = frames; }
}
static Actor* free_enemy()
{
    for (auto& e : enemies) if (!e.alive) return &e;
    return nullptr;
}

/* -------------------------------------------------- スポーン */
static void spawn_enemy(int type, float x)
{
    Actor* e = free_enemy();
    if (!e) return;
    float hpbase, w;
    switch (type) {
    case E_BAT:   hpbase = 12; w = 18; break;
    case E_GOLEM: hpbase = 60; w = 26; break;
    case E_BOSS:  hpbase = 200; w = 36; break;
    default:      hpbase = 20; w = 20; break;
    }
    float hpmax = hpbase + wave_n * (type == E_BOSS ? 30 : 6);
    *e = Actor();
    e->alive = true; e->type = (uint8_t)type;
    e->x = x; e->y = frand(BELT_Y0 + 10, BELT_Y1 - 10);
    e->hpmax = hpmax; e->hp = hpmax;
    e->dir = -1; e->state = A_WALK;
    if (type == E_BOSS) add_fx(e->x, e->y - 40, 3, 0, 90); /* "!" 出現演出 */
}

static void start_wave()
{
    wave_active = true;
    bool boss = (wave_n % 5 == 0);
    int n = 3 + (int)(wave_n / 2);
    if (n > 6) n = 6;
    if (boss) { spawn_enemy(E_BOSS, cam_x + 560); n = 2; }
    for (int i = 0; i < n; i++) {
        int t = E_SLIME;
        uint32_t r = rnd() % 100;
        if (wave_n >= 2 && r < 30) t = E_BAT;
        if (wave_n >= 3 && r >= 30 && r < 50) t = E_GOLEM;
        spawn_enemy(t, cam_x + 500 + frand(0, 260) + i * 30);
    }
    spawn_left = 0;
}

/* -------------------------------------------------- 公開 */
void init()
{
    player = Actor();
    player.alive = true; player.is_player = true;
    player.x = 200; player.y = 260; player.dir = 1;
    player.hpmax = 100 * gp::g.hp_pct / 100.0f;
    player.hp = player.hpmax;
    memset(enemies, 0, sizeof(enemies));
    memset(fxs, 0, sizeof(fxs));
    cam_x = 0; wave_n = 1; wave_timer = 60; wave_active = false;
    freeze = 0; shake = 0; shake_mag = 0; drops_pending = 0;
    pad = { 0, 0, false, false };
}

void refresh_player()
{
    float nm = 100 * gp::g.hp_pct / 100.0f;
    if (nm != player.hpmax) { player.hp = player.hp * nm / player.hpmax; player.hpmax = nm; }
}

void set_wave(uint32_t w) { wave_n = w ? w : 1; wave_timer = 30; wave_active = false; }
uint32_t wave() { return wave_n; }
float player_hp()    { return player.hp; }
float player_hpmax() { return player.hpmax; }
void  heal_player()  { player.hp = player.hpmax; }

/* -------------------------------------------------- プレイヤー攻撃判定 */
static void player_hit_check(Actor& p, int stage)
{
    float reach = 40 * gp::g.reach_pct / 100.0f;
    float dmg = 10 * gp::g.dmg_pct / 100.0f * (stage == 2 ? 2.0f : 1.0f);
    bool any = false;
    for (auto& e : enemies) {
        if (!e.alive || e.state == A_DOWN) continue;
        float dx = (p.x + reach / 2 * p.dir) - e.x;
        float dy = p.y - e.y;
        if (fabsf(dx) < reach / 2 + 16 && fabsf(dy) < 18) {
            any = true;
            e.hp -= dmg;
            e.flash = 3;
            e.state = A_HURT; e.timer = 14;
            e.vx = p.dir * (stage == 2 ? 5.0f : 2.6f);
            add_fx(e.x, e.y - 22, 2, 0, 3);                       /* ヒットマーク */
            add_fx(e.x, e.y - 34, 0, (int32_t)dmg, 30);           /* ダメージ数字 */
            freeze = (stage == 2) ? 6 : 4;                        /* ヒットストップ */
            if (e.hp <= 0) {
                e.alive = false;
                double drop = (e.type == E_BOSS ? 200 : 10) + wave_n * 2;
                game::add_drops(drop);
                add_fx(e.x, e.y - 26, 1, (int32_t)drop, 36);
                shake_add(e.type == E_BOSS ? 4 : 1, e.type == E_BOSS ? 20 : 5);
            } else if (stage == 2) {
                shake_add(1, 3);
            }
        }
    }
    (void)any;
}

static void player_step(float dt_norm)
{
    Actor& p = player;
    if (p.state == A_DOWN) {
        if (--p.timer == 0) {  /* リトライは迅速に (#116) */
            p.state = A_IDLE;
            p.hp = p.hpmax;
            p.invuln = true; p.timer = 60;
            p.x = cam_x + 120;
        }
        return;
    }
    if (p.invuln && p.state != A_HURT) { if (--p.timer == 0) p.invuln = false; }
    if (p.state == A_HURT) {
        if (--p.timer == 0) p.state = A_IDLE;
        p.x += p.vx; p.vx *= 0.85f;
        return;
    }
    if (p.state == A_ATK) {
        if (--p.timer == 0) {
            p.state = A_IDLE;
        }
        return;
    }
    /* 移動 */
    p.x += p.vx; p.vx *= 0.85f;   /* ダッシュ・ノックバックの慣性 */
    float sp = 1.6f * gp::g.move_pct / 100.0f;
    float mx = pad.dx * sp, my = pad.dy * sp * 0.7f;
    if (fabsf(mx) > 0.1f) p.dir = mx > 0 ? 1 : -1;
    p.x += mx; p.y += my;
    if (p.y < BELT_Y0) p.y = BELT_Y0;
    if (p.y > BELT_Y1) p.y = BELT_Y1;
    if (p.x < 8) p.x = 8;
    if (p.x > WORLD_W - 8) p.x = WORLD_W - 8;
    p.state = (fabsf(mx) + fabsf(my) > 0.2f) ? A_WALK : A_IDLE;
    if (pad.dash) { p.vx = p.dir * 7.0f; }   /* ダッシュ: 前方へ慣性バースト */

    /* 攻撃入力 (3 段コンボ) */
    if (pad.atk) {
        uint8_t next = (p.combo < 2) ? p.combo : 2;
        p.state = A_ATK;
        p.timer = 16;
        player_hit_check(p, next);
        p.combo = (next + 1) % 3;
    } else {
        /* 入力が途切れたらコンボリセット (簡易: 毎フレーム減衰は省略) */
        static uint16_t combo_gap = 0;
        if (p.state != A_ATK && ++combo_gap > 30) { p.combo = 0; combo_gap = 0; }
    }
}

static void enemy_step(Actor& e)
{
    Actor& p = player;
    switch (e.state) {
    case A_HURT:
        if (--e.timer == 0) e.state = A_WALK;
        e.x += e.vx; e.vx *= 0.85f;
        break;
    case A_WINDUP:
        if (--e.timer == 0) {
            /* 攻撃成立判定 */
            float dx = p.x - e.x, dy = p.y - e.y;
            float reach = (e.type == E_GOLEM || e.type == E_BOSS) ? 40 : 28;
            if (!p.invuln && p.state != A_DOWN && fabsf(dx) < reach && fabsf(dy) < 16) {
                float dmg = (e.type == E_BOSS) ? 14 : (e.type == E_GOLEM ? 10 : 5);
                p.hp -= dmg;
                p.flash = 3;
                p.state = A_HURT; p.timer = 16;
                p.vx = (dx > 0 ? 1 : -1) * 2.5f;
                add_fx(p.x, p.y - 40, 2, 0, 3);
                add_fx(p.x, p.y - 48, 0, -(int32_t)dmg, 30);
                freeze = 3;
                if (p.hp <= 0) { p.hp = 0; p.state = A_DOWN; p.timer = 60; shake_add(2, 8); }
            }
            e.state = A_RECOVER; e.timer = 30;
        }
        break;
    case A_RECOVER:
        if (--e.timer == 0) e.state = A_WALK;
        break;
    case A_WALK: {
        float dx = p.x - e.x, dy = p.y - e.y;
        float reach = (e.type == E_GOLEM || e.type == E_BOSS) ? 36 : 26;
        float sp = (e.type == E_BAT) ? 1.1f : (e.type == E_GOLEM ? 0.4f : (e.type == E_BOSS ? 0.5f : 0.6f));
        if (fabsf(dx) > reach) { e.x += (dx > 0 ? 1 : -1) * sp; e.dir = dx > 0 ? 1 : -1; }
        else if (fabsf(dy) > 12) { e.y += (dy > 0 ? 1 : -1) * sp * 0.6f; }
        else { e.state = A_WINDUP; e.timer = 24; }
        break;
    }
    default: break;
    }
    if (e.x < 4) e.x = 4;
    if (e.x > WORLD_W - 4) e.x = WORLD_W - 4;
    if (e.y < BELT_Y0) e.y = BELT_Y0;
    if (e.y > BELT_Y1) e.y = BELT_Y1;
}

static void fx_step()
{
    for (auto& f : fxs) {
        if (!f.on) continue;
        if (f.kind == 0 || f.kind == 1) { f.y -= 0.6f; }
        if (--f.life <= 0) f.on = false;
    }
}

void step()
{
    /* ヒットストップ: 全員停止 (演出時間を有効に使う) */
    if (freeze > 0) { freeze--; return; }

    player_step(1.0f);
    for (auto& e : enemies) if (e.alive) enemy_step(e);
    fx_step();

    /* ウェーブ管理 */
    int alive = 0;
    for (auto& e : enemies) if (e.alive) alive++;
    if (wave_active) {
        if (alive == 0) { wave_active = false; wave_n++; wave_timer = 180; }
    } else {
        if (wave_timer > 0) wave_timer--;
        else start_wave();
    }

    /* カメラ追従 */
    float target = player.x - 180;
    if (target < 0) target = 0;
    if (target > WORLD_W - FB_W) target = WORLD_W - FB_W;
    cam_x += (target - cam_x) * 0.15f;

    if (shake > 0) shake--;
    if (player.flash) player.flash--;
    for (auto& e : enemies) if (e.flash) e.flash--;

    pad.atk = false; pad.dash = false;
}

/* -------------------------------------------------- 描画 (プレースホルダ図形。スプライトは P3' で差し替え) */
static void draw_shadow(uint16_t* fb, int sx, int sy, int w)
{
    fill_rect(fb, sx - w / 2, sy - 2, w, 3, RGB(0x14, 0x22, 0x1C));
}

static void draw_player(uint16_t* fb, Actor& p)
{
    int sx = (int)(p.x - cam_x), sy = (int)p.y;
    if (sx < -40 || sx > FB_W + 40) return;
    if (p.invuln && (rnd() & 2)) return;  /* 無敵点滅 */
    uint16_t hair = p.flash ? C_WHITE : C_HAIR;
    uint16_t body = p.flash ? C_WHITE : C_DRESS;
    int d = p.dir;
    draw_shadow(fb, sx, sy, 24);
    /* 脚 */
    fill_rect(fb, sx - 6, sy - 8, 4, 8, p.flash ? C_WHITE : RGB(0xF0, 0xE0, 0xC0));
    fill_rect(fb, sx + 2, sy - 8, 4, 8, p.flash ? C_WHITE : RGB(0xF0, 0xE0, 0xC0));
    /* ワンピ (台形) */
    for (int i = 0; i < 14; i++)
        fill_rect(fb, sx - (8 + i / 3), sy - 8 - i, (16 + (i / 3) * 2), 1, body);
    /* 頭 */
    fill_circle(fb, sx, sy - 28, 9, hair);
    fill_circle(fb, sx + d * 3, sy - 26, 5, p.flash ? C_WHITE : C_SKIN);
    fill_rect(fb, sx + d * 4, sy - 27, 2, 2, C_AQUA);            /* 目 */
    /* ツインテール */
    fill_circle(fb, sx - d * 8, sy - 26, 4, hair);
    /* 腕 (攻撃中は伸ばす) */
    if (p.state == A_ATK) {
        int ext = 14 + p.timer / 4;
        fill_rect(fb, sx + (d > 0 ? 8 : -8 - ext), sy - 18, ext, 4, C_SKIN);
    } else {
        fill_rect(fb, sx + (d > 0 ? 6 : -9), sy - 18, 4, 8, C_SKIN);
    }
}

/* WINDUP の予備動作点滅用 */
static uint32_t blink_tick = 0;
static bool tick_blink() { return ((blink_tick++) / 4) % 2 == 0; }

static void draw_enemy(uint16_t* fb, Actor& e)
{
    int sx = (int)(e.x - cam_x), sy = (int)e.y;
    if (sx < -50 || sx > FB_W + 50) return;
    uint16_t c;
    int w, h, ey;
    switch (e.type) {
    case E_BAT:   c = RGB(0x9C, 0x6C, 0xD8); w = 18; h = 14; break;
    case E_GOLEM: c = RGB(0x8A, 0x8A, 0x78); w = 26; h = 30; break;
    case E_BOSS:  c = RGB(0xD0, 0x40, 0x60); w = 40; h = 44; break;
    default:      c = RGB(0x50, 0xC0, 0x60); w = 20; h = 16; break;
    }
    if (e.flash) c = C_WHITE;
    if (e.state == A_WINDUP && (tick_blink())) c = C_WHITE;
    draw_shadow(fb, sx, sy, w);
    fill_circle(fb, sx, sy - h / 2 - 2, h / 2, c);
    /* 目 (プレイヤー方向) */
    uint16_t eye = RGB(0x20, 0x20, 0x28);
    int d = (player.x > e.x) ? 1 : -1;
    if (!e.flash) {
        fill_rect(fb, sx + d * 2 - 1, sy - h / 2 - 4, 3, 3, eye);
        fill_rect(fb, sx + d * 6 - 1, sy - h / 2 - 4, 3, 3, eye);
    }
    /* HP バー (ダメージ後のみ) */
    if (e.hp < e.hpmax) {
        int bw = w + 8;
        fill_rect(fb, sx - bw / 2, sy - h - 10, bw, 3, C_BLACK);
        fill_rect(fb, sx - bw / 2 + 1, sy - h - 9, (int)((bw - 2) * e.hp / e.hpmax), 1, C_RED);
    }
}

static void draw_fx(uint16_t* fb, Fx& f)
{
    int sx = (int)(f.x - cam_x), sy = (int)f.y;
    switch (f.kind) {
    case 0: /* ダメージ数字 (白、クリ幅大は不要 v0) */
        draw_number(fb, sx, sy, f.v, 2, C_WHITE);
        break;
    case 1: /* クリスタルドロップ */
        draw_number(fb, sx, sy, 10, 1, C_AQUA);
        draw_digit(fb, sx - 6, sy, 10, 2, C_AQUA);
        break;
    case 2: /* ヒットマーク: 星4本+黒縁 (#130 明暗どちらでも) */
        fill_circle(fb, sx, sy, 2, C_WHITE);
        for (int i = 0; i < 4; i++) {
            int dx = (i == 0) - (i == 1), dy = (i == 2) - (i == 3);
            fill_rect(fb, sx + dx * 5, sy + dy * 5, dx ? 5 : 2, dy ? 5 : 2, C_BLACK);
            fill_rect(fb, sx + dx * 5 + (dx < 0 ? 1 : 0), sy + dy * 5 + (dy < 0 ? 1 : 0), dx ? 4 : 1, dy ? 4 : 1, C_WHITE);
        }
        break;
    case 3: /* ボス出現 "!" */
        fill_rect(fb, sx - 2, sy, 5, 12, C_RED);
        fill_rect(fb, sx - 2, sy + 16, 5, 5, C_RED);
        break;
    }
}

void render(uint16_t* fb)
{
    /* 空 */
    fill_rect(fb, 0, 0, FB_W, FB_H, C_SKY);
    /* 遠景 (パララックス 0.4) */
    for (int i = 0; i < 10; i++) {
        float wx = i * 240.0f;
        int sx = (int)(wx - cam_x * 0.4f) % (FB_W + 240) - 120;
        fill_rect(fb, sx, 60, 36, 120, C_FAR);
        fill_rect(fb, sx - 8, 90, 52, 8, C_FAR);
    }
    /* 歩行帯 */
    fill_rect(fb, 0, (int)BELT_Y0, FB_W, (int)(BELT_Y1 - BELT_Y0) + 10, C_GRASS);
    for (int i = 0; i < 30; i++) {
        int gx = (int)(i * 64 - fmodf(cam_x, 64));
        if (gx > -64 && gx < FB_W) fill_rect(fb, gx, (int)BELT_Y0 + 20, 24, 2, C_GRASS2);
    }
    /* 画面振動オフセット (#103 小=縦のみ) */
    int ox = 0, oy = 0;
    if (shake > 0) {
        oy = (rnd() % (shake_mag * 2 + 1)) - shake_mag;
        if (shake_mag > 2) ox = (rnd() % (shake_mag * 2 + 1)) - shake_mag;
    }

    /* アクタを y ソートして描画 (奥から手前) */
    Actor* order[9];
    int n = 0;
    for (auto& e : enemies) if (e.alive) order[n++] = &e;
    order[n++] = &player;
    for (int i = 1; i < n; i++) {          /* 挿入ソート (n<=9) */
        Actor* k = order[i];
        int j = i - 1;
        while (j >= 0 && order[j]->y > k->y) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    for (int i = 0; i < n; i++) {
        Actor* a = order[i];
        if (!a->is_player) draw_enemy(fb, *a); else draw_player(fb, *a);
    }
    (void)ox; (void)oy;
    /* 振動は簡易: 全体オフセットは P2' で本実装 (現状は縦のみ点滅的揺れ) */
    if (oy | ox) { /* v0: 振動は無視せず次フレームのカメラ揺れに変換済み */ }

    /* FX */
    for (auto& f : fxs) if (f.on) draw_fx(fb, f);
}

} // namespace beat
