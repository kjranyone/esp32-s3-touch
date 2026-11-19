/**
 * web_main.cpp — 『ルミの水晶庭園』v4.0 Web (Emscripten/WASM) デバイスシミュレータ
 *
 * 実機 (JingCai ESP32-4848S040C_I_Y_3, 480x480 RGB565 + GT911 タッチ) と
 * 同一の C++ ゲームコア (beat.cpp / garden_game.cpp / game_params.cpp) を
 * そのまま WASM 化して HTML5 Canvas 上で実行する。
 */
#include <Arduino.h>
#include <esp_timer.h>
#include <emscripten.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "beat.h"
#include "sfx.h"
#include "game_params.h"
#include "garden_game.h"

WebSerial Serial;

extern "C" void web_serial_emit(const char* text)
{
    if (!text || !*text) return;
    EM_ASM({
        if (typeof window !== 'undefined' && typeof window.onEmulatorSerial === 'function') {
            window.onEmulatorSerial(UTF8ToString($0));
        }
    }, text);
}

/* ---------------- 480x480 ダブルバッファ & RGBA 出力 ---------------- */
static constexpr int SCR_W = 480;
static constexpr int SCR_H = 480;
static constexpr int SCR_PIXELS = SCR_W * SCR_H;

static uint16_t* canvas[2] = {nullptr, nullptr};
static uint32_t* rgba_buf = nullptr;
static uint32_t  rgb565_lut[65536];
static int render_idx = 0;
static int scan_idx = 1;

static void init_rgb565_lut()
{
    for (uint32_t c = 0; c < 65536; ++c) {
        uint32_t r = ((c >> 11) & 0x1Fu);
        uint32_t g = ((c >> 5)  & 0x3Fu);
        uint32_t b = ( c        & 0x1Fu);
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        /* Little-endian RGBA8888 (ImageData バイト順: R, G, B, A) */
        rgb565_lut[c] = (0xFFu << 24) | (b << 16) | (g << 8) | r;
    }
}

static void convert_to_rgba(const uint16_t* src)
{
    uint32_t* dst = rgba_buf;
    for (int i = 0; i < SCR_PIXELS; ++i) {
        dst[i] = rgb565_lut[src[i]];
    }
}

/* ---------------- UI & ポインタ状態 ---------------- */
static bool panel_open = false;
static bool info_peek = false;
static bool title_confirm = false;
static int64_t last_save_t = 0;

static bool    sw_active = false;
static int16_t sw_x0 = 0, sw_y0 = 0, sw_dx = 0, sw_dy = 0;
static int64_t sw_t0 = 0;

/* ---------------- シリアルコマンド保留状態 ---------------- */
static double   pend_give = 0;
static bool     pend_give_on = false;
static uint32_t pend_wave = 0;
static bool     pend_save = false;
static bool     pend_heal = false;
static bool     pend_boss = false;
static int      pend_bphase = 0;
static bool     pend_box = false;
static int      pend_pick_c = -1;
static int      pend_pick_slot = -1;
static int      pend_scout = -1;
static int      pend_charm = -1;
static bool     pend_ult = false;
static bool     pend_win = false;
static bool     pend_gameover = false;
static bool     pend_ending = false;
static bool     pend_unlockall = false;
static bool     pend_hardreset = false;
static bool     pend_reset = false;
static bool     pend_title = false;
static bool     pend_op = false;
static bool     pend_map = false;
static bool     pend_status = false;
static int      pend_go_lane = -1;
static bool     pend_tap = false;
static int      pend_tap_x = 0, pend_tap_y = 0;

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
            if (x >= beat::CLOSE_X && x < beat::CLOSE_X + beat::CLOSE_W &&
                y >= beat::CLOSE_Y && y < beat::CLOSE_Y + beat::CLOSE_H) {
                title_confirm = false;
                sfx::play(sfx::SFX_CANCEL);
            }
            return;
        }

        /* [X] 閉じる */
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

static void handle_command_line(char* line)
{
    while (*line == ' ') line++;
    if (*line == 0) return;
    if (strcmp(line, "?") == 0 || strcmp(line, "help") == 0) {
        gp::print_all();
        Serial.println("[cmd] give <v> | wave <1..50> | boss | bphase <50|30|10> | box | pick <0..2> [slot] | scout <id> | charm <id>");
        Serial.println("[cmd] ult | win | heal | gameover | ending | title | op | map | unlockall | hardreset | reset | save | dump");
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
            Serial.printf("[sfx] playing chest fanfare (sr=%uHz)\n", (unsigned)sfx::sample_rate());
        } else {
            int sid = atoi(arg);
            if (sid >= 0 && sid < sfx::SFX_COUNT) {
                sfx::play((sfx::Id)sid);
                Serial.printf("[sfx] play #%d (%s)\n", sid, sfx::name((sfx::Id)sid));
            }
        }
        return;
    }
    if (strncmp(line, "go ", 3) == 0)   { pend_go_lane = atoi(line + 3); return; }
    if (strncmp(line, "tap ", 4) == 0)  { sscanf(line + 4, "%d %d", &pend_tap_x, &pend_tap_y); pend_tap = true; return; }
    if (strcmp(line, "reset") == 0)     { pend_reset = true; return; }
    if (strcmp(line, "hardreset") == 0) { pend_hardreset = true; return; }
    if (strcmp(line, "unlockall") == 0) { pend_unlockall = true; return; }
    if (strcmp(line, "dump") == 0)      { beat::dump(); return; }
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
    if (!gp::apply(line, v)) Serial.printf("[cmd] unknown key: %s\n", line);
}

static void apply_pending_commands()
{
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
        Serial.printf("[cmd] pick %d -> res=%d\n", c, res);
    }
    if (pend_scout >= 0) {
        int tid = pend_scout; pend_scout = -1;
        if (beat::is_title()) beat::set_wave(beat::wave());
        if (game::debug_grant_talent(tid)) {
            beat::on_draft_pick(1);
            Serial.printf("[cmd] scouted #%d %s\n", tid, game::TALENTS[tid].name);
        }
    }
    if (pend_charm >= 0) {
        int cid = pend_charm; pend_charm = -1;
        if (beat::is_title()) beat::set_wave(beat::wave());
        if (game::debug_grant_charm(cid)) {
            beat::on_draft_pick(4);
            Serial.printf("[cmd] equipped charm #%d %s\n", cid, game::CHARMS[cid].name);
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
    if (pend_go_lane >= 0) {
        int ln = pend_go_lane; pend_go_lane = -1;
        panel_open = false; info_peek = false;
        beat::force_travel(ln);
    }
    if (pend_tap) {
        pend_tap = false;
        handle_tap(pend_tap_x, pend_tap_y);
    }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void emu_init()
{
    init_rgb565_lut();
    canvas[0] = (uint16_t*)calloc(SCR_PIXELS, sizeof(uint16_t));
    canvas[1] = (uint16_t*)calloc(SCR_PIXELS, sizeof(uint16_t));
    rgba_buf  = (uint32_t*)calloc(SCR_PIXELS, sizeof(uint32_t));

    Serial.println("== Crystal Garden v4.0: 50-Wave Roguelite (Web Simulator) ==");
    Serial.println("Target Hardware: JingCai ESP32-4848S040C_I_Y_3 (480x480 RGB565 / GT911)");

    sfx::init();
    game::init();
    beat::init();
    beat::set_wave(game::st.wave_reached ? game::st.wave_reached : 1);
    beat::enter_title();
    game::save();

    beat::render(canvas[1] + 56 * SCR_W, 1);
    beat::render_hud(canvas[1], 1);
    convert_to_rgba(canvas[1]);
    scan_idx = 1;
    render_idx = 0;
    last_save_t = esp_timer_get_time();

    Serial.println("Setup done. Type 'help' or '?' for serial commands.");
}

EMSCRIPTEN_KEEPALIVE
void emu_step(float dt_sec)
{
    if (dt_sec <= 0.0f) dt_sec = 0.0166667f;
    if (dt_sec > 0.2f) dt_sec = 0.2f;

    apply_pending_commands();
    game::tick(dt_sec);
    if (!panel_open) {
        beat::step();
    }

    if (!beat::allow_box_button() && beat::is_modal_director() && panel_open) {
        panel_open = false;
        info_peek = false;
        game::st.swap_talent = -1;
        beat::request_full_restore();
    }

    int64_t now = esp_timer_get_time();
    if (now - last_save_t >= 30000000LL) {
        last_save_t = now;
        game::save();
    }
}

EMSCRIPTEN_KEEPALIVE
void emu_render()
{
    uint16_t* target = canvas[render_idx];
    beat::render(target + 56 * SCR_W, render_idx);
    beat::render_hud(target, render_idx);
    beat::render_panel(target, panel_open, info_peek, render_idx, title_confirm);

    convert_to_rgba(target);
    scan_idx = render_idx;
    render_idx ^= 1;
}

EMSCRIPTEN_KEEPALIVE
uint8_t* emu_get_rgba_ptr()
{
    return reinterpret_cast<uint8_t*>(rgba_buf);
}

EMSCRIPTEN_KEEPALIVE
void emu_pointer_down(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= SCR_W) x = SCR_W - 1;
    if (y < 0) y = 0;
    if (y >= SCR_H) y = SCR_H - 1;
    sw_x0 = (int16_t)x;
    sw_y0 = (int16_t)y;
    sw_dx = 0;
    sw_dy = 0;
    sw_t0 = esp_timer_get_time();
    sw_active = true;
}

EMSCRIPTEN_KEEPALIVE
void emu_pointer_move(int x, int y)
{
    if (!sw_active) return;
    sw_dx = (int16_t)(x - sw_x0);
    sw_dy = (int16_t)(y - sw_y0);
}

EMSCRIPTEN_KEEPALIVE
void emu_pointer_up(int x, int y)
{
    if (!sw_active) return;
    sw_dx = (int16_t)(x - sw_x0);
    sw_dy = (int16_t)(y - sw_y0);
    sw_active = false;

    int64_t dt_ms = (esp_timer_get_time() - sw_t0) / 1000;
    if (!panel_open && !beat::is_modal_director() &&
        dt_ms < 2000 && abs(sw_dy) > 60 && abs(sw_dy) > 2 * abs(sw_dx) &&
        sw_y0 > 56 && sw_y0 < 408) {
        game::pet_trigger();
        beat::on_pet();
        char msg[24];
        snprintf(msg, sizeof(msg), "PET! +%d%%", (int)gp::g.pet_buff_pct);
        beat::flash_message(msg, 45);
    } else if (abs(sw_dy) <= 60) {
        handle_tap(sw_x0, sw_y0);
    }
    sw_dx = 0;
    sw_dy = 0;
}

EMSCRIPTEN_KEEPALIVE
void emu_trigger_pet()
{
    if (panel_open || beat::is_modal_director()) return;
    game::pet_trigger();
    beat::on_pet();
    char msg[24];
    snprintf(msg, sizeof(msg), "PET! +%d%%", (int)gp::g.pet_buff_pct);
    beat::flash_message(msg, 45);
}

EMSCRIPTEN_KEEPALIVE
void emu_send_command(const char* cmd)
{
    if (!cmd) return;
    char buf[128];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.printf("> %s\n", buf);
    handle_command_line(buf);
}

EMSCRIPTEN_KEEPALIVE
void emu_save()
{
    game::save();
}

EMSCRIPTEN_KEEPALIVE
uint32_t emu_get_wave() { return beat::wave(); }

EMSCRIPTEN_KEEPALIVE
uint32_t emu_get_run() { return game::st.run_number; }

static float audio_f32_buf[2048];

EMSCRIPTEN_KEEPALIVE
void emu_audio_set_rate(int hz)
{
    sfx::set_sample_rate((uint32_t)(hz > 0 ? hz : 44100));
}

EMSCRIPTEN_KEEPALIVE
float* emu_audio_render(int frames)
{
    if (frames < 0) frames = 0;
    if (frames > 2048) frames = 2048;
    sfx::render_f32(audio_f32_buf, frames);
    return audio_f32_buf;
}

EMSCRIPTEN_KEEPALIVE
void emu_sfx_set_muted(int muted)
{
    sfx::set_muted(muted != 0);
}

EMSCRIPTEN_KEEPALIVE
int emu_sfx_is_muted()
{
    return sfx::is_muted() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void emu_sfx_play(int id)
{
    if (id >= 0 && id < sfx::SFX_COUNT) sfx::play((sfx::Id)id);
}

EMSCRIPTEN_KEEPALIVE
int emu_get_party_count() { return game::party_count(); }

} // extern "C"
