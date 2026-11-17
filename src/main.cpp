/**
 * 『ルミの水晶庭園』v3 — ベルトスクロールアクション + 放置収入 + スキルツリー
 *
 * - 左仮想パッド: 8 方向移動 / ATK: 3 段コンボ / DASH: 慣性バースト
 * - キャンバス縦スワイプ: なでなでバフ
 * - ヒットストップ・ヒットマーク・画面振動・ダメージ数字 (桜井スキル適用)
 * - スキルツリー UI (NVS セーブ) / シリアル調整コンソール
 */
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "beat.h"
#include "garden_game.h"
#include "game_params.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>
#include <math.h>
#include <assert.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* ================= UI ハンドル ================= */
static lv_obj_t* canvas_obj = nullptr;
static uint16_t* canvas_buf = nullptr;
static lv_obj_t* hp_bar = nullptr;
static lv_obj_t* crystals_label = nullptr;
static lv_obj_t* wave_label = nullptr;
static lv_obj_t* next_label = nullptr;
static lv_obj_t* pet_label = nullptr;
static lv_obj_t* skill_panel = nullptr;
static lv_obj_t* node_btns[game::SKILL_N];
static lv_obj_t* node_sub[game::SKILL_N];

/* なでなでジェスチャ */
static int16_t sw_x0 = 0, sw_y0 = 0, sw_dx = 0, sw_dy = 0;
static int64_t sw_t0 = 0;
static bool sw_active = false;

/* シリアルコンソールからの保留コマンド (LVGL スレッドで実行) */
static volatile double pend_give = 0;
static volatile bool pend_give_on = false;
static volatile int pend_skill = -1;
static volatile uint32_t pend_wave = 0;
static volatile bool pend_save = false;
static volatile bool pend_heal = false;

/* なでなでポップアップ */
static lv_obj_t* pet_pop = nullptr;
static uint32_t pet_pop_until = 0;
static uint32_t frame_n = 0;

/* フレーム統計 */
static uint32_t frame_count = 0;
static int64_t fps_t0 = 0, last_tick_t = 0;
static uint32_t fps_value = 0;
static uint32_t hud_div = 0, save_div = 0, report_div = 0;

static char fmt_buf[2][24];

/* ================= イベント ================= */
static void pad_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) { beat::pad.dx = beat::pad.dy = 0; return; }
    if (code != LV_EVENT_PRESSING) return;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    float dx = (p.x - 83) / 55.0f, dy = (p.y - 443) / 28.0f;
    if (dx > 1) dx = 1; if (dx < -1) dx = -1;
    if (dy > 1) dy = 1; if (dy < -1) dy = -1;
    if (fabsf(dx) < 0.15f) dx = 0;
    if (fabsf(dy) < 0.15f) dy = 0;
    beat::pad.dx = dx; beat::pad.dy = dy;
}

static void atk_cb(lv_event_t* e) { (void)e; beat::pad.atk = true; }
static void dash_cb(lv_event_t* e) { (void)e; beat::pad.dash = true; }

/* キャンバスの縦スワイプ = なでなで (#104 反応必須) */
static void canvas_swipe_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        sw_x0 = p.x; sw_y0 = p.y; sw_dx = sw_dy = 0;
        sw_t0 = esp_timer_get_time();
        sw_active = true;
    } else if (code == LV_EVENT_PRESSING && sw_active) {
        sw_dx = p.x - sw_x0; sw_dy = p.y - sw_y0;
    } else if (code == LV_EVENT_RELEASED && sw_active) {
        sw_active = false;
        int64_t dt = (esp_timer_get_time() - sw_t0) / 1000;
        if (dt < 2000 && abs(sw_dy) > 60 && abs(sw_dy) > 2 * abs(sw_dx)) {
            game::pet_trigger();
            if (pet_pop) {
                lv_label_set_text_fmt(pet_pop, "PET! +%d%%", (int)gp::g.pet_buff_pct);
                lv_obj_clear_flag(pet_pop, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(pet_pop, 200, 90);
                pet_pop_until = frame_n + 45;
            }
        }
    }
}

static void skill_btn_cb(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (game::st.owned[idx]) return;                       /* 購入済みは反応なし扱い */
    if (game::skill_locked(idx)) {
        Serial.printf("[skill] %s is locked\r\n", game::SKILLS[idx].name);
        return;
    }
    if (game::buy_skill(idx)) {
        beat::refresh_player();
        Serial.printf("[skill] bought %s\r\n", game::SKILLS[idx].name);
    }
}

static void open_skill_cb(lv_event_t* e)
{
    (void)e;
    if (lv_obj_has_flag(skill_panel, LV_OBJ_FLAG_HIDDEN))
        lv_obj_clear_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
}

static void close_skill_cb(lv_event_t* e)
{
    (void)e;
    lv_obj_add_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
}

/* ================= パネル更新 (1Hz) ================= */
static void refresh_skill_panel()
{
    for (int i = 0; i < game::SKILL_N; i++) {
        lv_obj_t* sub = node_sub[i];
        if (!sub) continue;
        if (game::st.owned[i]) {
            lv_label_set_text(sub, "OWNED");
            lv_obj_set_style_text_color(sub, lv_color_hex(0x8FE8A0), 0);
            lv_obj_set_style_bg_color(node_btns[i], lv_color_hex(0x1E5A34), 0);
        } else if (game::skill_locked(i)) {
            lv_label_set_text(sub, "LOCKED");
            lv_obj_set_style_text_color(sub, lv_color_hex(0x888898), 0);
            lv_obj_set_style_bg_color(node_btns[i], lv_color_hex(0x282834), 0);
        } else {
            bool ok = game::skill_affordable(i);
            lv_label_set_text_fmt(sub, "%s %s", game::fmt(game::SKILLS[i].cost, fmt_buf[0], 24),
                                  ok ? "" : "(low)");
            lv_obj_set_style_text_color(sub, lv_color_hex(ok ? 0xFFFFFF : 0xE07070), 0);
            lv_obj_set_style_bg_color(node_btns[i], lv_color_hex(0x1C3A50), 0);
        }
    }
}

static void refresh_hud()
{
    /* HP バー */
    float hp = beat::player_hp(), hpmax = beat::player_hpmax();
    if (hpmax > 0) lv_bar_set_value(hp_bar, hp / hpmax * 100.0f, LV_ANIM_OFF);

    /* クリスタル + 放置収入 */
    game::fmt(game::st.crystals, fmt_buf[0], 24);
    lv_label_set_text_fmt(crystals_label, "C %s  (+%.1f/s)", fmt_buf[0], game::income_rate());

    /* ウェーブ */
    lv_label_set_text_fmt(wave_label, "WAVE %u", (unsigned)beat::wave());

    /* 次の目標 (#023) */
    double diff = game::next_goal(fmt_buf[0], 24);
    lv_label_set_text_fmt(next_label, "NEXT: %s (-%s)", fmt_buf[0],
                          game::fmt(diff, fmt_buf[1], 24));

    /* なでなでバフ (常時インジケータ) */
    if (game::pet_active())
        lv_label_set_text_fmt(pet_label, "PET %d:%02d",
                              game::pet_remaining_s() / 60, game::pet_remaining_s() % 60);
    else
        lv_obj_add_flag(pet_label, LV_OBJ_FLAG_HIDDEN);

    refresh_skill_panel();
}

/* ================= 毎フレーム ================= */
static void tick_cb(lv_timer_t* timer)
{
    (void)timer;
    int64_t now = esp_timer_get_time();
    float dt = (last_tick_t == 0) ? 0.033f : (now - last_tick_t) / 1000000.0f;
    if (dt > 0.2f) dt = 0.2f;
    last_tick_t = now;

    /* 保留コマンド (シリアル → LVGL スレッド) */
    if (pend_give_on) { pend_give_on = false; game::debug_give(pend_give); }
    if (pend_skill >= 0) {
        int i = pend_skill; pend_skill = -1;
        if (game::buy_skill(i)) { beat::refresh_player(); Serial.printf("[skill] bought %s\r\n", game::SKILLS[i].name); }
        else Serial.println("[skill] cannot buy");
    }
    if (pend_wave) { pend_wave = 0; }
    if (pend_heal) { pend_heal = false; beat::heal_player(); }
    if (pend_save) { pend_save = false; game::save(); Serial.println("[game] saved"); }
    if (pend_wave) {
        beat::set_wave(pend_wave);
        game::st.wave_reached = pend_wave;
        pend_wave = 0;
    }
    frame_n++;
    if (pet_pop && pet_pop_until && frame_n >= pet_pop_until) {
        lv_obj_add_flag(pet_pop, LV_OBJ_FLAG_HIDDEN);
        pet_pop_until = 0;
    }

    /* ゲーム進行 */
    game::tick(dt);
    beat::step();
    beat::render(canvas_buf);
    lv_obj_invalidate(canvas_obj);

    /* FPS */
    frame_count++;
    if (fps_t0 == 0) fps_t0 = now;
    if (now - fps_t0 >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / (now - fps_t0));
        frame_count = 0; fps_t0 = now;
    }

    /* HUD (1Hz) */
    if (++hud_div >= 30) { hud_div = 0; refresh_hud(); }

    /* オートセーブ (30s) */
    if (++save_div >= 900) { save_div = 0; game::save(); }

    /* シリアルステータス (10s) */
    if (++report_div >= 600) {
        report_div = 0;
        game::fmt(game::st.crystals, fmt_buf[0], 24);
        Serial.printf("[beat] fps=%u wave=%u crystals=%s hp=%.0f/%.0f\r\n",
                      (unsigned)fps_value, (unsigned)beat::wave(), fmt_buf[0],
                      beat::player_hp(), beat::player_hpmax());
    }
}

/* ================= UI 構築 ================= */
static lv_obj_t* make_label(lv_obj_t* parent, const char* text, int x, int y, int font, uint32_t color)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    const lv_font_t* f = (font == 8) ? &lv_font_montserrat_8
                       : (font == 20) ? &lv_font_montserrat_20
                       : (font == 14) ? &lv_font_montserrat_14
                       : &lv_font_montserrat_16;
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void build_ui()
{
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101018), 0);

    /* --- HUD (y 0..55) --- */
    lv_obj_t* hud = lv_obj_create(scr);
    lv_obj_remove_style_all(hud);
    lv_obj_set_pos(hud, 0, 0);
    lv_obj_set_size(hud, 480, 56);

    hp_bar = lv_bar_create(hud);
    lv_obj_set_pos(hp_bar, 8, 6);
    lv_obj_set_size(hp_bar, 140, 16);
    lv_bar_set_range(hp_bar, 0, 100);
    lv_bar_set_value(hp_bar, 100, LV_ANIM_OFF);

    crystals_label = make_label(hud, "C 0  (+0.5/s)", 160, 2, 16, 0xA8E8F0);
    wave_label = make_label(hud, "WAVE 1", 340, 2, 14, 0xE8D8A0);
    next_label = make_label(hud, "NEXT: -", 160, 28, 14, 0xC0C0D0);
    pet_label = make_label(hud, "", 340, 28, 14, 0xF0A0B8);

    lv_obj_t* skill_btn = lv_btn_create(hud);
    lv_obj_set_pos(skill_btn, 412, 4);
    lv_obj_set_size(skill_btn, 62, 44);
    lv_obj_add_event_cb(skill_btn, open_skill_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* sl = lv_label_create(skill_btn);
    lv_label_set_text(sl, "SKILL");
    lv_obj_center(sl);

    /* --- ゲームキャンバス (y 56..407) --- */
    canvas_buf = (uint16_t*)heap_caps_calloc(beat::FB_W * beat::FB_H,
                                             sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(canvas_buf);
    canvas_obj = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas_obj, canvas_buf, beat::FB_W, beat::FB_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(canvas_obj, 0, 56);
    lv_obj_clear_flag(canvas_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(canvas_obj, canvas_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(canvas_obj, canvas_swipe_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas_obj, canvas_swipe_cb, LV_EVENT_RELEASED, NULL);

    /* --- 仮想パッド (y 408..479) --- */
    lv_obj_t* pad_zone = lv_obj_create(scr);
    lv_obj_set_style_bg_color(pad_zone, lv_color_hex(0x222230), 0);
    lv_obj_set_style_bg_opa(pad_zone, LV_OPA_50, 0);
    lv_obj_set_style_radius(pad_zone, 36, 0);
    lv_obj_set_pos(pad_zone, 8, 408);
    lv_obj_set_size(pad_zone, 150, 68);
    lv_obj_add_event_cb(pad_zone, pad_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pad_zone, pad_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(pad_zone, pad_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_clear_flag(pad_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* pl = lv_label_create(pad_zone);
    lv_label_set_text(pl, LV_SYMBOL_LEFT "  " LV_SYMBOL_RIGHT);
    lv_obj_center(pl);

    /* --- ATK / DASH ボタン --- */
    lv_obj_t* atk = lv_btn_create(scr);
    lv_obj_set_pos(atk, 330, 412);
    lv_obj_set_size(atk, 140, 60);
    lv_obj_add_event_cb(atk, atk_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t* al = lv_label_create(atk);
    lv_label_set_text(al, "ATK");
    lv_obj_center(al);

    lv_obj_t* dash = lv_btn_create(scr);
    lv_obj_set_pos(dash, 200, 418);
    lv_obj_set_size(dash, 100, 48);
    lv_obj_add_event_cb(dash, dash_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t* dl = lv_label_create(dash);
    lv_label_set_text(dl, "DASH");
    lv_obj_center(dl);

    /* --- スキルパネル (オーバーレイ, 戦闘は裏で継続 = 放置) --- */
    skill_panel = lv_obj_create(scr);
    lv_obj_set_pos(skill_panel, 0, 56);
    lv_obj_set_size(skill_panel, 480, 352);
    lv_obj_set_style_bg_color(skill_panel, lv_color_hex(0x141420), 0);
    lv_obj_set_style_bg_opa(skill_panel, LV_OPA_90, 0);
    lv_obj_add_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(skill_panel, LV_OBJ_FLAG_SCROLLABLE);

    make_label(skill_panel, "SKILL TREE", 10, 4, 16, 0x9FE8D8);
    lv_obj_t* close = lv_btn_create(skill_panel);
    lv_obj_set_pos(close, 424, 2);
    lv_obj_set_size(close, 48, 26);
    lv_obj_add_event_cb(close, close_skill_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cl = lv_label_create(close);
    lv_label_set_text(cl, "X");
    lv_obj_center(cl);

    /* ノード配置: col0 = 根 (0,2,5,7), col1 = 枝 (1,4,3,6,8) */
    static const int col0[4] = {0, 2, 5, 7};
    static const int col1[5] = {1, 4, 3, 6, 8};
    for (int r = 0; r < 4; r++) {
        int i = col0[r];
        lv_obj_t* b = lv_btn_create(skill_panel);
        lv_obj_set_pos(b, 16, 36 + r * 76);
        lv_obj_set_size(b, 200, 64);
        lv_obj_add_event_cb(b, skill_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* n = make_label(b, game::SKILLS[i].name, 8, 6, 14, 0xFFFFFF);
        node_sub[i] = make_label(b, "", 8, 30, 14, 0xFFFFFF);
        node_btns[i] = b;
        (void)n;
    }
    for (int r = 0; r < 5; r++) {
        int i = col1[r];
        lv_obj_t* b = lv_btn_create(skill_panel);
        lv_obj_set_pos(b, 260, 10 + r * 68);
        lv_obj_set_size(b, 200, 60);
        lv_obj_add_event_cb(b, skill_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        make_label(b, game::SKILLS[i].name, 8, 6, 14, 0xFFFFFF);
        node_sub[i] = make_label(b, "", 8, 30, 14, 0xFFFFFF);
        node_btns[i] = b;
    }

    /* なでなでポップアップ (canvas 上) */
    pet_pop = make_label(scr, "", 200, 90, 20, 0xFF9AB8);
    lv_obj_add_flag(pet_pop, LV_OBJ_FLAG_HIDDEN);

    /* --- 物理フレームタイマ (約30fps) --- */
    lv_timer_create(tick_cb, 33, NULL);
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
        Serial.println("[cmd] give <v> | buyskill <i> | wave <n> | heal | save | reset");
        return;
    }
    if (strcmp(line, "reset") == 0) { beat::init(); Serial.println("[cmd] battle reset"); return; }
    if (strncmp(line, "give ", 5) == 0)   { pend_give = atof(line + 5); pend_give_on = true; return; }
    if (strncmp(line, "buyskill ", 9) == 0){ pend_skill = atoi(line + 9); return; }
    if (strncmp(line, "wave ", 5) == 0)   { pend_wave = (uint32_t)atoi(line + 5); return; }
    if (strcmp(line, "heal") == 0)        { pend_heal = true; return; }
    if (strcmp(line, "save") == 0)        { pend_save = true; return; }
    char* sp = strchr(line, ' ');
    if (!sp) sp = strchr(line, '=');
    if (!sp) { Serial.println("[cmd] usage: <key> <value> | ? | reset"); return; }
    *sp = 0;
    long v = atol(sp + 1);
    if (!gp::apply(line, v)) Serial.printf("[cmd] unknown key: %s\r\n", line);
}

void loop()
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
    delay(2);
}

/* ================= 起動 ================= */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("== Crystal Garden v3: belt-scroll action ==");
    Serial.printf("Chip %s rev%d, SPIRAM %u bytes free\r\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    Serial.println("Initializing board");
    Board* board = new Board();
    board->init();
    assert(board->begin());

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Building UI");
    lvgl_port_lock(-1);
    game::init();
    beat::init();
    beat::set_wave(game::st.wave_reached ? game::st.wave_reached : 1);
    build_ui();
    lvgl_port_unlock();

    Serial.println("Setup done");
}
