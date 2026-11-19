/**
 * 『ルミの水晶庭園』v4.0 — 全50ウェーブ・ローグライト オートバトラー
 *
 * LVGL 完全排除。ESP_Panel 所有のダブルバッファ FB へ 32行内部SRAM バンド合成器で
 * 直接描画し、ST7701 RGB パネル (98Hz バウンスDMA) とティアリングゼロで同期する。
 */
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include "beat.h"
#include "sfx.h"
#include "garden_game.h"
#include "game_params.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_cache.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <math.h>
#include <assert.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static Board* g_board = nullptr;
static LCD* g_lcd = nullptr;
static Touch* g_touch = nullptr;
static uint16_t* canvas[2] = {nullptr, nullptr};
static int render_idx = 0;
static int scan_idx = 0;
static SemaphoreHandle_t fb_done_sem = nullptr;

IRAM_ATTR static bool on_refresh_finish(void*)
{
    BaseType_t hpw = pdFALSE;
    if (fb_done_sem != nullptr) {
        xSemaphoreGiveFromISR(fb_done_sem, &hpw);
    }
    return hpw == pdTRUE;
}

/* ---------------- UI & プロファイル状態 ---------------- */
static bool panel_open = false;
static bool info_peek = false;
static bool title_confirm = false;
static int64_t last_frame_t = 0;
static uint32_t frame_count = 0;
static int64_t fps_t0 = 0;
static uint32_t fps_value = 0;
static int64_t last_save_t = 0, last_status_t = 0;
static int64_t prof_render = 0;
static uint32_t prof_frames = 0;
static int64_t prof_wait = 0, prof_sync = 0, prof_present = 0, prof_total = 0;
static int64_t prof_battle = 0, prof_hud = 0, prof_panel = 0;

/* ---------------- タッチ入力 ---------------- */
struct TouchInput {
    bool pressed;
    int x, y;
};
static TouchInput tin;
static int16_t sw_x0 = 0, sw_y0 = 0, sw_dx = 0, sw_dy = 0;
static int64_t sw_t0 = 0;
static bool sw_active = false;

/* ---------------- シリアルデバッグ保留コマンド ---------------- */
static volatile double   pend_give = 0;
static volatile bool     pend_give_on = false;
static volatile uint32_t pend_wave = 0;
static volatile bool     pend_save = false;
static volatile bool     pend_heal = false;
static volatile bool     pend_boss = false;
static volatile int      pend_bphase = 0;
static volatile bool     pend_box = false;
static volatile int      pend_pick_c = -1;
static volatile int      pend_pick_slot = -1;
static volatile int      pend_scout = -1;
static volatile int      pend_charm = -1;
static volatile bool     pend_ult = false;
static volatile bool     pend_win = false;
static volatile bool     pend_gameover = false;
static volatile bool     pend_ending = false;
static volatile bool     pend_unlockall = false;
static volatile bool     pend_hardreset = false;
static volatile bool     pend_reset = false;
static volatile bool     pend_title = false;
static volatile bool     pend_op = false;
static volatile bool     pend_map = false;
static volatile bool     pend_status = false;
static volatile bool     shot_req = false;
static volatile bool     pause_render = false;

/* ================= タッチ処理 ================= */
static void handle_tap(int x, int y)
{
    /* BOX ボタン (右下): 戦闘中 & マップ画面でいつでも安全に開閉可 */
    if (beat::allow_box_button() &&
        x >= beat::BOXBTN_X && x < beat::BOXBTN_X + beat::BOXBTN_W &&
        y >= beat::BOXBTN_Y && y < beat::BOXBTN_Y + beat::BOXBTN_H) {
        if (!panel_open) {
            panel_open = true;
            info_peek = false;
            title_confirm = false;
            bool opened_box = false;
            if (!game::st.draft_open && game::can_open_box()) {
                if (game::open_lootbox(false)) {
                    beat::flash_message("LOOTBOX OPEN!", 36);
                    opened_box = true;
                }
            }
            sfx::play((opened_box || game::st.draft_open) ? sfx::SFX_BOX_OPEN : sfx::SFX_PAUSE);
        } else {
            panel_open = false;
            info_peek = false;
            title_confirm = false;
            game::st.swap_talent = -1;
            sfx::play(sfx::SFX_CANCEL);
        }
        beat::request_full_restore();
        return;
    }

    if (!panel_open && beat::is_modal_director()) {
        panel_open = false;
        info_peek = false;
        beat::notify_tap(x, y);
        return;
    }

    if (panel_open) {
        /* タイトル復帰・ゲームリセット警告ダイアログ表示中 */
        if (title_confirm) {
            if (x >= beat::DLG_CANCEL_X && x < beat::DLG_CANCEL_X + beat::DLG_CANCEL_W &&
                y >= beat::DLG_CANCEL_Y && y < beat::DLG_CANCEL_Y + beat::DLG_CANCEL_H) {
                title_confirm = false;
                sfx::play(sfx::SFX_CANCEL);
                return;
            }
            if (x >= beat::DLG_OK_X && x < beat::DLG_OK_X + beat::DLG_OK_W &&
                y >= beat::DLG_OK_Y && y < beat::DLG_OK_Y + beat::DLG_OK_H) {
                sfx::play(sfx::SFX_CONFIRM);
                game::on_run_end(beat::wave(), false);
                game::start_new_run();
                beat::sync_party(true);
                beat::set_wave(1);
                panel_open = false;
                info_peek = false;
                title_confirm = false;
                game::st.swap_talent = -1;
                beat::enter_title();
                return;
            }
            /* ダイアログ外 (×ボタン含む) はキャンセル扱い */
            if (x >= beat::CLOSE_X && x < beat::CLOSE_X + beat::CLOSE_W &&
                y >= beat::CLOSE_Y && y < beat::CLOSE_Y + beat::CLOSE_H) {
                title_confirm = false;
                sfx::play(sfx::SFX_CANCEL);
            }
            return;
        }

        /* [X] 閉じる (入れ替え選択中はドラフトへ戻る) */
        if (x >= beat::CLOSE_X && x < beat::CLOSE_X + beat::CLOSE_W &&
            y >= beat::CLOSE_Y && y < beat::CLOSE_Y + beat::CLOSE_H) {
            sfx::play(sfx::SFX_CANCEL);
            if (game::st.swap_talent >= 0) {
                game::st.swap_talent = -1;
            } else {
                panel_open = false;
                info_peek = false;
                title_confirm = false;
                beat::request_full_restore();
            }
            return;
        }

        /* PAUSED / STATUS 画面の [TITLE] ボタン (リセット警告ダイアログを開く) */
        bool on_status_view = (!game::st.draft_open || info_peek) && (game::st.swap_talent < 0);
        if (on_status_view &&
            x >= beat::TITLEBTN_X && x < beat::TITLEBTN_X + beat::TITLEBTN_W &&
            y >= beat::TITLEBTN_Y && y < beat::TITLEBTN_Y + beat::TITLEBTN_H) {
            title_confirm = true;
            sfx::play(sfx::SFX_TAP);
            return;
        }

        /* PAUSED / STATUS 画面の [SFX:ON/OFF] トグルボタン */
        if (on_status_view &&
            x >= beat::SFXBTN_X && x < beat::SFXBTN_X + beat::SFXBTN_W &&
            y >= beat::SFXBTN_Y && y < beat::SFXBTN_Y + beat::SFXBTN_H) {
            bool next_mute = !sfx::is_muted();
            sfx::set_muted(next_mute);
            if (!next_mute) sfx::play(sfx::SFX_CONFIRM);
            return;
        }

        /* ヘッダ [STATUS / TO DRAFT / OPEN BOX] */
        if (game::st.swap_talent < 0 &&
            x >= beat::PEEKBTN_X && x < beat::PEEKBTN_X + beat::PEEKBTN_W &&
            y >= beat::PEEKBTN_Y && y < beat::PEEKBTN_Y + beat::PEEKBTN_H) {
            if (game::st.draft_open) {
                info_peek = !info_peek;
                sfx::play(sfx::SFX_TAP);
            } else if (game::can_open_box()) {
                if (game::open_lootbox(false)) {
                    info_peek = false;
                    sfx::play(sfx::SFX_BOX_OPEN);
                }
            }
            return;
        }

        /* 満員入替先スロット選択 */
        if (game::st.draft_open && game::st.swap_talent >= 0) {
            for (int i = 0; i < game::PARTY_MAX; ++i) {
                int sx = beat::SWAP_X[i], sy = beat::SWAP_Y;
                if (x >= sx && x < sx + beat::SWAP_W && y >= sy && y < sy + beat::SWAP_H) {
                    int c_idx = 0;
                    for (int c = 0; c < 3; ++c) {
                        if (game::st.draft[c].is_talent && game::st.draft[c].id == game::st.swap_talent) {
                            c_idx = c;
                            break;
                        }
                    }
                    int res = game::pick_draft(c_idx, i);
                    if (res >= 1 && res <= 4) {
                        beat::on_draft_pick(res);
                        panel_open = false;
                        info_peek = false;
                        beat::request_full_restore();
                    }
                    return;
                }
            }
            return;
        }

        /* 3択ドラフトカード選択 */
        if (game::st.draft_open && !info_peek) {
            for (int c = 0; c < 3; ++c) {
                int cx = beat::CARD_X[c], cy = beat::CARD_Y;
                if (x >= cx && x < cx + beat::CARD_W && y >= cy && y < cy + beat::CARD_H) {
                    int res = game::pick_draft(c, -1);
                    if (res >= 1 && res <= 4) {
                        beat::on_draft_pick(res);
                        panel_open = false;
                        info_peek = false;
                        beat::request_full_restore();
                    }
                    return;
                }
            }
        }
    } else {
        beat::notify_tap(x, y);
    }
}

static void poll_touch()
{
    if (!g_touch) return;
    TouchPoint pts[2];
    int n = g_touch->readPoints(pts, 2);

    bool pressed = (n > 0);
    bool new_press = (pressed && !tin.pressed);
    bool released = (!pressed && tin.pressed);
    tin.pressed = pressed;
    if (pressed) { tin.x = pts[0].x; tin.y = pts[0].y; }

    if (new_press) {
        sw_x0 = tin.x; sw_y0 = tin.y; sw_dx = sw_dy = 0;
        sw_t0 = esp_timer_get_time();
        sw_active = true;
    } else if (sw_active && tin.pressed) {
        sw_dx = tin.x - sw_x0;
        sw_dy = tin.y - sw_y0;
    }

    if (released) {
        if (sw_active) {
            sw_active = false;
            int64_t dt = (esp_timer_get_time() - sw_t0) / 1000;
            if (!panel_open && !beat::is_modal_director() &&
                dt < 2000 && abs(sw_dy) > 60 && abs(sw_dy) > 2 * abs(sw_dx) && sw_y0 > 56 && sw_y0 < 408) {
                game::pet_trigger();
                beat::on_pet();
                char msg[24];
                snprintf(msg, sizeof(msg), "PET! +%d%%", (int)gp::g.pet_buff_pct);
                beat::flash_message(msg, 45);
            } else if (abs(sw_dy) <= 60) {
                handle_tap(sw_x0, sw_y0);
            }
        }
        sw_dx = sw_dy = 0;
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
        Serial.println("[cmd] give <v> | wave <1..50> | boss | bphase <50|30|10> | box | pick <0..2> [slot] | scout <id> | charm <id>");
        Serial.println("[cmd] ult | win | heal | gameover | ending | title | op | map | unlockall | hardreset | reset | save | dump | shot");
        return;
    }
    if (strcmp(line, "title") == 0)     { pend_title = true; return; }
    if (strcmp(line, "op") == 0)        { pend_op = true; return; }
    if (strcmp(line, "map") == 0)       { pend_map = true; return; }
    if (strcmp(line, "status") == 0)    { pend_status = true; return; }
    if (strncmp(line, "sfx", 3) == 0 && (line[3] == 0 || line[3] == ' ')) {
        const char* arg = line[3] ? (line + 4) : "";
        if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
            sfx::set_muted(true);
            Serial.println("[sfx] muted");
        } else if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
            sfx::set_muted(false);
            sfx::play(sfx::SFX_CONFIRM);
            Serial.println("[sfx] unmuted");
        } else if (strcmp(arg, "test") == 0 || *arg == 0) {
            sfx::set_muted(false);
            sfx::play(sfx::SFX_CHEST_OPEN);
            Serial.printf("[sfx] playing chest fanfare (sr=%uHz)\r\n", (unsigned)sfx::sample_rate());
        } else {
            int sid = atoi(arg);
            if (sid >= 0 && sid < sfx::SFX_COUNT) {
                sfx::play((sfx::Id)sid);
                Serial.printf("[sfx] play #%d (%s)\r\n", sid, sfx::name((sfx::Id)sid));
            }
        }
        return;
    }
    if (strcmp(line, "reset") == 0)     { pend_reset = true; return; }
    if (strcmp(line, "hardreset") == 0) { pend_hardreset = true; return; }
    if (strcmp(line, "unlockall") == 0) { pend_unlockall = true; return; }
    if (strcmp(line, "dump") == 0)      { beat::dump(); return; }
    if (strcmp(line, "shot") == 0)      { shot_req = true; return; }
    if (strcmp(line, "heal") == 0)      { pend_heal = true; return; }
    if (strcmp(line, "boss") == 0)      { pend_boss = true; return; }
    if (strcmp(line, "box") == 0)       { pend_box = true; return; }
    if (strcmp(line, "ult") == 0)       { pend_ult = true; return; }
    if (strcmp(line, "win") == 0)       { pend_win = true; return; }
    if (strcmp(line, "gameover") == 0)  { pend_gameover = true; return; }
    if (strcmp(line, "ending") == 0)    { pend_ending = true; return; }
    if (strcmp(line, "save") == 0)      { pend_save = true; return; }
    if (strncmp(line, "give ", 5) == 0)   { pend_give = atof(line + 5); pend_give_on = true; return; }
    if (strncmp(line, "wave ", 5) == 0)   { pend_wave = (uint32_t)atoi(line + 5); return; }
    if (strncmp(line, "bphase ", 7) == 0) { pend_bphase = atoi(line + 7); return; }
    if (strncmp(line, "scout ", 6) == 0)  { pend_scout = atoi(line + 6); return; }
    if (strncmp(line, "charm ", 6) == 0)  { pend_charm = atoi(line + 6); return; }
    if (strncmp(line, "pick ", 5) == 0) {
        int c = -1, s = -1;
        sscanf(line + 5, "%d %d", &c, &s);
        pend_pick_c = c;
        pend_pick_slot = s;
        return;
    }
    char* sp = strchr(line, ' ');
    if (!sp) sp = strchr(line, '=');
    if (!sp) { Serial.println("[cmd] usage: <key> <value> | ? | help"); return; }
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
    uint16_t* p = canvas[scan_idx];
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

    if (pend_hardreset) {
        pend_hardreset = false;
        panel_open = false; info_peek = false;
        game::hard_reset();
        beat::init();
        beat::enter_title();
    }
    if (pend_reset) {
        pend_reset = false;
        panel_open = false; info_peek = false;
        game::start_new_run();
        beat::init();
    }
    if (pend_unlockall) {
        pend_unlockall = false;
        game::unlock_all_meta();
        Serial.println("[cmd] unlocked all 64 talents & 36 charms in pool");
    }
    if (pend_give_on) { pend_give_on = false; game::debug_give(pend_give); }
    if (pend_wave) {
        uint32_t w = pend_wave; pend_wave = 0;
        panel_open = false;
        beat::set_wave(w);
    }
    if (pend_heal) { pend_heal = false; beat::heal_all(); }
    if (pend_boss) { pend_boss = false; panel_open = false; beat::force_boss(); }
    if (pend_bphase > 0) { int pct = pend_bphase; pend_bphase = 0; panel_open = false; beat::force_boss_phase(pct); }
    if (pend_box) {
        pend_box = false;
        if (beat::is_title()) beat::set_wave(beat::wave());
        if (!game::st.draft_open) game::open_lootbox(true);
        panel_open = true; info_peek = false;
        beat::request_full_restore();
        Serial.println("[cmd] opened lootbox draft");
    }
    if (pend_status) {
        pend_status = false;
        if (beat::is_title()) beat::set_wave(beat::wave());
        panel_open = true; info_peek = true;
        beat::request_full_restore();
    }
    if (pend_pick_c >= 0) {
        int c = pend_pick_c, s = pend_pick_slot; pend_pick_c = -1; pend_pick_slot = -1;
        int res = game::pick_draft(c, s);
        if (res >= 1 && res <= 4) {
            beat::on_draft_pick(res);
            panel_open = false; info_peek = false;
            beat::request_full_restore();
        }
        Serial.printf("[cmd] pick %d -> res=%d\r\n", c, res);
    }
    if (pend_scout >= 0) {
        int tid = pend_scout; pend_scout = -1;
        if (beat::is_title()) beat::set_wave(beat::wave());
        if (game::debug_grant_talent(tid)) {
            beat::on_draft_pick(1);
            Serial.printf("[cmd] scouted #%d %s\r\n", tid, game::TALENTS[tid].name);
        }
    }
    if (pend_charm >= 0) {
        int cid = pend_charm; pend_charm = -1;
        if (beat::is_title()) beat::set_wave(beat::wave());
        if (game::debug_grant_charm(cid)) {
            beat::on_draft_pick(4);
            Serial.printf("[cmd] equipped charm #%d %s\r\n", cid, game::CHARMS[cid].name);
        }
    }
    if (pend_ult)  { pend_ult = false;  if (beat::is_title()) beat::set_wave(beat::wave()); beat::force_ult(); }
    if (pend_win)  { pend_win = false;  if (beat::is_title()) beat::set_wave(beat::wave()); beat::force_win(); }
    if (pend_gameover) { pend_gameover = false; panel_open = false; beat::force_gameover(); }
    if (pend_ending)   { pend_ending = false; panel_open = false; beat::force_ending(); }
    if (pend_save) { pend_save = false; game::save(); Serial.println("[game] saved"); }
    if (pend_title) {
        pend_title = false;
        panel_open = false; info_peek = false;
        beat::enter_title();
    }
    if (pend_op) {
        pend_op = false;
        panel_open = false; info_peek = false;
        beat::force_opening();
    }
    if (pend_map) {
        pend_map = false;
        panel_open = false; info_peek = false;
        beat::force_map();
    }
    if (shot_req) { shot_req = false; do_shot(); }

    poll_touch();
    game::tick(dt);
    if (!panel_open) {
        beat::step();
    }

    if (!beat::allow_box_button() && beat::is_modal_director() && panel_open) {
        panel_open = false;
        info_peek = false;
        game::st.swap_talent = -1;
        beat::request_full_restore();
    }

    int64_t wait_start = esp_timer_get_time();
    bool can_draw = (xSemaphoreTake(fb_done_sem, pdMS_TO_TICKS(30)) == pdTRUE);
    prof_wait += esp_timer_get_time() - wait_start;
    if (!can_draw) return;

    uint16_t* target = canvas[render_idx];
    int64_t render_start = esp_timer_get_time();
    beat::render(target + 56 * 480, render_idx);
    int64_t t_battle = esp_timer_get_time();
    beat::render_hud(target, render_idx);
    int64_t t_hud = esp_timer_get_time();
    beat::render_panel(target, panel_open, info_peek, render_idx, title_confirm);
    int64_t t_panel = esp_timer_get_time();
    prof_render += t_panel - render_start;
    prof_battle += t_battle - render_start;
    prof_hud += t_hud - t_battle;
    prof_panel += t_panel - t_hud;
    prof_frames++;

    int64_t sync_start = esp_timer_get_time();
    esp_cache_msync((void*)target, 480 * 480 * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    int64_t present_start = esp_timer_get_time();
    prof_sync += present_start - sync_start;

    while (xSemaphoreTake(fb_done_sem, 0) == pdTRUE) {}
    g_lcd->switchFrameBufferTo(target);
    while (xSemaphoreTake(fb_done_sem, 0) == pdTRUE) {}
    prof_present += esp_timer_get_time() - present_start;
    prof_total += esp_timer_get_time() - now;
    scan_idx = render_idx;
    render_idx ^= 1;

    frame_count++;
    if (fps_t0 == 0) fps_t0 = now;
    if (now - fps_t0 >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / (now - fps_t0));
        frame_count = 0; fps_t0 = now;
    }

    if (now - last_save_t >= 30000000LL) {
        last_save_t = now;
        game::save();
    }

    if (now - last_status_t >= 10000000LL) {
        last_status_t = now;
        char b1[24];
        Serial.printf("[beat] fps=%u run=%u wave=%u/50 party=%d crystals=%s hp0=%.0f/%.0f rage=%.0f/%.0f/%.0f\r\n",
                      (unsigned)fps_value, (unsigned)game::st.run_number, (unsigned)beat::wave(),
                      game::party_count(), game::fmt(game::st.crystals, b1, sizeof(b1)),
                      beat::ally_hp(0), beat::ally_hpmax(0),
                      beat::ally_rage(0), beat::ally_rage(1), beat::ally_rage(2));
        Serial.printf("[prof] render=%ldus fps=%ld | int_free=%u int_max=%u psram_free=%u\r\n",
                      (long)(prof_render / (prof_frames ? prof_frames : 1)),
                      (long)fps_value,
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        int64_t rs_us = 0, dr_us = 0;
        beat::render_stats(&rs_us, &dr_us);
        Serial.printf("[pipeline] battle=%lld(rstr=%lld draw=%lld) hud=%lld panel=%lld sync=%lld present=%lld wait=%lld total=%lld cpu=%uMHz\n",
                      prof_battle / (prof_frames ? prof_frames : 1), rs_us, dr_us,
                      prof_hud / (prof_frames ? prof_frames : 1), prof_panel / (prof_frames ? prof_frames : 1),
                      prof_sync / (prof_frames ? prof_frames : 1),
                      prof_present / (prof_frames ? prof_frames : 1), prof_wait / (prof_frames ? prof_frames : 1),
                      prof_total / (prof_frames ? prof_frames : 1), getCpuFrequencyMhz());
        prof_render = 0; prof_frames = 0;
        prof_wait = prof_sync = prof_present = prof_total = 0;
        prof_battle = prof_hud = prof_panel = 0;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("== Crystal Garden v4.0: 50-Wave Roguelite ==");
    Serial.printf("Chip %s rev%d, SPIRAM %u bytes free\r\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    g_board = new Board();
    g_board->init();
    g_lcd = g_board->getLCD();
    g_touch = g_board->getTouch();

    auto* rgb_bus = static_cast<BusRGB*>(g_lcd->getBus());
    assert(rgb_bus->configRGB_FrameBufferNumber(2));

    fb_done_sem = xSemaphoreCreateBinary();
    assert(fb_done_sem);
    assert(g_lcd->attachRefreshFinishCallback(on_refresh_finish));
    assert(g_board->begin());

    canvas[0] = (uint16_t*)g_lcd->getFrameBufferByIndex(0);
    canvas[1] = (uint16_t*)g_lcd->getFrameBufferByIndex(1);
    assert(canvas[0] && canvas[1]);

    sfx::init();
    game::init();
    beat::init();
    beat::set_wave(game::st.wave_reached ? game::st.wave_reached : 1);
    beat::enter_title();
    game::save();

    beat::render(canvas[1] + 56 * 480, 1);
    beat::render_hud(canvas[1], 1);
    esp_cache_msync((void*)canvas[1], 480 * 480 * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    while (xSemaphoreTake(fb_done_sem, 0) == pdTRUE) {}
    g_lcd->switchFrameBufferTo(canvas[1]);
    while (xSemaphoreTake(fb_done_sem, 0) == pdTRUE) {}
    scan_idx = 1;
    render_idx = 0;
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
