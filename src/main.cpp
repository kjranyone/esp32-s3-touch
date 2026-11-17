/**
 * 『ルミの水晶庭園』v3 — ベルトスクロール・オートバトラー (放置系)
 *
 * アーキテクチャ: LVGL 完全排除。フレームバッファ (480x480 PSRAM) を単一の
 * 書き込み元 (このゲーム) が所有し、毎フレーム キャッシュ同期 → スキャン切替。
 * すべての描画 (ゲーム/HUD/スキルパネル) は fb 直接描画。
 * タッチは ESP_Panel の Touch ドライバを直接ポーリング。
 */
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include "beat.h"
#include "garden_game.h"
#include "game_params.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_cache.h>
#include <string.h>
#include <math.h>
#include <assert.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static Board* g_board = nullptr;
static LCD* g_lcd = nullptr;
static Touch* g_touch = nullptr;
static void* canvas_raw = nullptr;
static uint16_t* canvas = nullptr;   /* 480x480 全面バッファ (スキャン対象, 64B アライン) */

/* ---------------- 状態 ---------------- */
static bool skill_open = false;
static int64_t last_frame_t = 0;
static uint32_t frame_count = 0;
static int64_t fps_t0 = 0;
static uint32_t fps_value = 0;
static int64_t last_save_t = 0, last_status_t = 0;
static int64_t prof_step = 0, prof_render = 0;
static uint32_t prof_frames = 0;

static char fmt_buf[2][24];

/* ---------------- タッチ入力 ---------------- */
struct TouchInput {
    bool pressed;
    int x, y;
    bool new_press;      /* このフレームで押された */
    bool released;       /* このフレームで離された */
};
static TouchInput tin;

/* スワイプ (なでなで) 検出 */
static int16_t sw_x0 = 0, sw_y0 = 0, sw_dx = 0, sw_dy = 0;
static int64_t sw_t0 = 0;
static bool sw_active = false;

/* シリアルコンソールからの保留コマンド */
static volatile double pend_give = 0;
static volatile bool pend_give_on = false;
static volatile int pend_skill = -1;
static volatile uint32_t pend_wave = 0;
static volatile bool pend_save = false;
static volatile bool pend_heal = false;
static volatile bool shot_req = false;
static volatile bool pause_render = false;

/* ================= タッチ処理 ================= */
static int64_t tap_t0 = 0;

static void try_buy(int i);

static void handle_tap(int x, int y)
{
    if (skill_open) {
        /* パネル内 */
        if (x >= beat::CLOSE_X && x < beat::CLOSE_X + beat::CLOSE_W &&
            y >= beat::CLOSE_Y && y < beat::CLOSE_Y + beat::CLOSE_H) {
            skill_open = false;
            beat::request_full_restore();
            return;
        }
        for (int r = 0; r < 4; r++) {
            int i = beat::PANEL_COL0[r];
            int nx = beat::NODE0_X, ny = beat::NODE0_Y0 + r * beat::NODE0_PITCH;
            if (x >= nx && x < nx + beat::NODE_W && y >= ny && y < ny + beat::NODE_H) {
                try_buy(i);
                return;
            }
        }
        for (int r = 0; r < 5; r++) {
            int i = beat::PANEL_COL1[r];
            int nx = beat::NODE1_X, ny = beat::NODE1_Y0 + r * beat::NODE1_PITCH;
            if (x >= nx && x < nx + beat::NODE_W && y >= ny && y < ny + beat::NODE_H) {
                try_buy(i);
                return;
            }
        }
    } else {
        /* SKILL ボタン */
        if (x >= beat::SKILLBTN_X && x < beat::SKILLBTN_X + beat::SKILLBTN_W &&
            y >= beat::SKILLBTN_Y && y < beat::SKILLBTN_Y + beat::SKILLBTN_H) {
            skill_open = true;
            beat::request_full_restore();
        }
    }
}

static void poll_touch()
{
    TouchPoint pts[2];
    int n = g_touch->readPoints(pts, 2);

    bool pressed = (n > 0);
    bool new_press = (pressed && !tin.pressed);
    bool released = (!pressed && tin.pressed);
    tin.pressed = pressed;
    if (pressed) { tin.x = pts[0].x; tin.y = pts[0].y; }

    /* スワイプ追跡 (ゲーム領域のみ) */
    if (new_press && tin.y > 56 && tin.y < 408) {
        sw_x0 = tin.x; sw_y0 = tin.y; sw_dx = sw_dy = 0;
        sw_t0 = esp_timer_get_time();
        sw_active = true;
        tap_t0 = sw_t0;
    } else if (sw_active && tin.pressed) {
        sw_dx = tin.x - sw_x0;
        sw_dy = tin.y - sw_y0;
    }

    if (released) {
        if (sw_active) {
            sw_active = false;
            int64_t dt = (esp_timer_get_time() - sw_t0) / 1000;
            /* 縦スワイプ = なでなで (#104 反応必須) */
            if (dt < 2000 && abs(sw_dy) > 60 && abs(sw_dy) > 2 * abs(sw_dx) && sw_y0 > 56 && sw_y0 < 408) {
                game::pet_trigger();
                char msg[24];
                snprintf(msg, sizeof(msg), "PET! +%d%%", (int)gp::g.pet_buff_pct);
                beat::flash_message(msg, 45);
            }
            /* 縦スワイプでなければタップとして処理 */
            else if (abs(sw_dy) <= 60) {
                handle_tap(sw_x0, sw_y0);
            }
        }
        sw_dx = sw_dy = 0;
    }
}

static void try_buy(int i)
{
    if (game::st.owned[i]) {
        Serial.printf("[skill] %s: OWNED\r\n", game::SKILLS[i].name);
        return;
    }
    if (game::buy_skill(i)) {
        beat::refresh_player();
        char b[24];
        Serial.printf("[skill] bought %s (C %s)\r\n", game::SKILLS[i].name,
                      game::fmt(game::st.crystals, b, sizeof(b)));
    } else {
        Serial.printf("[skill] %s: cannot buy\r\n", game::SKILLS[i].name);
    }
}

/* ================= シリアル調整コンソール ================= */
static char cmd_buf[64];
static size_t cmd_len = 0;

static void handle_command(char* line)
{
    while (*line == ' ') line++;
    if (*line == 0) return;
    if (strcmp(line, "?") == 0 || strcmp(line, "help") == 0) {
        gp::print_all();
        Serial.println("[cmd] give <v> | buyskill <i> | wave <n> | heal | save | reset | dump | shot");
        return;
    }
    if (strcmp(line, "reset") == 0) { beat::init(); Serial.println("[cmd] battle reset"); return; }
    if (strcmp(line, "dump") == 0)  { beat::dump(); return; }
    if (strcmp(line, "shot") == 0)  { shot_req = true; return; }
    if (strcmp(line, "heal") == 0)  { pend_heal = true; return; }
    if (strcmp(line, "save") == 0)  { pend_save = true; return; }
    if (strncmp(line, "give ", 5) == 0)     { pend_give = atof(line + 5); pend_give_on = true; return; }
    if (strncmp(line, "buyskill ", 9) == 0) { pend_skill = atoi(line + 9); return; }
    if (strncmp(line, "wave ", 5) == 0)     { pend_wave = (uint32_t)atoi(line + 5); return; }
    char* sp = strchr(line, ' ');
    if (!sp) sp = strchr(line, '=');
    if (!sp) { Serial.println("[cmd] usage: <key> <value> | ? | reset"); return; }
    *sp = 0;
    long v = atol(sp + 1);
    if (!gp::apply(line, v)) Serial.printf("[cmd] unknown key: %s\r\n", line);
}

static void pump_serial()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            cmd_buf[cmd_len] = 0;
            handle_command(cmd_buf);
            cmd_len = 0;
        } else if (cmd_len < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_len++] = c;
        }
    }
}

/* ================= スクリーンショット ================= */
static void do_shot()
{
    pause_render = true;
    delay(80);
    Serial.printf("SHOT 480 480\n");
    static uint8_t sbuf[512];
    int bl = 0;
    uint32_t total = 480 * 480, i = 0;
    uint16_t* p = canvas;
    while (i < total) {
        uint16_t c = p[i];
        uint32_t run = 1;
        while (i + run < total && p[i + run] == c && run < 65535) run++;
        sbuf[bl++] = run & 0xFF; sbuf[bl++] = (run >> 8) & 0xFF;
        sbuf[bl++] = (uint8_t)(c & 0xFF); sbuf[bl++] = (uint8_t)(c >> 8);
        i += run;
        if (bl >= 508) { Serial.write(sbuf, bl); bl = 0; }
    }
    if (bl) Serial.write(sbuf, bl);
    Serial.print("\nRLEEND\n");
    pause_render = false;
}

/* ================= フレーム ================= */
static void frame()
{
    int64_t now = esp_timer_get_time();
    float dt = (last_frame_t == 0) ? 0.016f : (now - last_frame_t) / 1000000.0f;
    if (dt > 0.2f) dt = 0.2f;
    last_frame_t = now;

    /* 保留コマンド */
    if (pend_give_on) { pend_give_on = false; game::debug_give(pend_give); }
    if (pend_skill >= 0) {
        int i = pend_skill; pend_skill = -1;
        if (game::buy_skill(i)) beat::refresh_player();
        else Serial.println("[skill] cannot buy");
    }
    if (pend_heal) { pend_heal = false; beat::heal_all(); }
    if (pend_save) { pend_save = false; game::save(); Serial.println("[game] saved"); }
    if (pend_wave) {
        beat::set_wave(pend_wave);
        game::st.wave_reached = pend_wave;
        pend_wave = 0;
    }
    if (shot_req) { shot_req = false; do_shot(); }

    /* 進行 */
    game::tick(dt);
    beat::step();
    int64_t t0 = esp_timer_get_time();
    beat::render(canvas + 56 * 480);    beat::render_hud(canvas);
    beat::render_panel(canvas, skill_open);
    /* キャッシュ書き戻し + スキャン切替 */
    esp_cache_msync((void *)canvas, 480 * 480 * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    g_lcd->switchFrameBufferTo(canvas);
    int64_t t1 = esp_timer_get_time();

    prof_render += (t1 - t0);
    prof_frames++;

    /* FPS (1s) */
    frame_count++;
    if (fps_t0 == 0) fps_t0 = now;
    if (now - fps_t0 >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / (now - fps_t0));
        frame_count = 0; fps_t0 = now;
    }

    /* オートセーブ (30s) */
    if (now - last_save_t >= 30000000LL) {
        last_save_t = now;
        game::save();
    }

    /* シリアルステータス (10s) */
    if (now - last_status_t >= 10000000LL) {
        last_status_t = now;
        char b1[24];
        Serial.printf("[beat] fps=%u wave=%u crystals=%s lumiHP=%.0f rage=%.0f/%.0f/%.0f\r\n",
                      (unsigned)fps_value, (unsigned)beat::wave(), game::fmt(game::st.crystals, b1, 24),
                      beat::ally_hp(0), beat::ally_rage(0), beat::ally_rage(1), beat::ally_rage(2));
        Serial.printf("[prof] render=%ldus fps=%ld | int_free=%u int_max=%u psram_free=%u\r\n",
                      (long)(prof_render / (prof_frames ? prof_frames : 1)),
                      (long)fps_value,
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        prof_render = 0; prof_frames = 0;
    }
}

/* ================= 起動 ================= */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("== Crystal Garden v3.1: auto battle (watch & grow) ==");
    Serial.printf("Chip %s rev%d, SPIRAM %u bytes free\r\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    Serial.println("Initializing board");
    g_board = new Board();
    g_board->init();
    assert(g_board->begin());
    g_lcd = g_board->getLCD();
    g_touch = g_board->getTouch();

    canvas_raw = heap_caps_calloc(480 * 480 * 2 + 64, 1, MALLOC_CAP_SPIRAM);
    assert(canvas_raw);
    canvas = (uint16_t *)(((uintptr_t)canvas_raw + 63) & ~(uintptr_t)63);   /* 64B アライン */
    g_lcd->switchFrameBufferTo(canvas);   /* スキャン対象を自前バッファへ */

    game::init();
    beat::init();
    beat::set_wave(game::st.wave_reached ? game::st.wave_reached : 1);
    game::save();

    Serial.println("Setup done");
}

void loop()
{
    pump_serial();

    if (shot_req) { shot_req = false; do_shot(); }

    int64_t now = esp_timer_get_time();
    if (last_frame_t != 0 && now - last_frame_t < 15000) {
        delay(1);
        return;
    }
    frame();
}
