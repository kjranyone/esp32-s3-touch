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

/* パネルへ直接描画するためのハンドル (LVGL を経由しない) */
static esp_panel::drivers::LCD* g_lcd = nullptr;

/* ================= UI ハンドル ================= */
static lv_obj_t* canvas_obj = nullptr;
static uint16_t* game_fb = nullptr;   /* パネルのフレームバッファ (直接描画) */
static lv_obj_t* hp_bar = nullptr;
static lv_obj_t* crystals_label = nullptr;
static lv_obj_t* wave_label = nullptr;
static lv_obj_t* next_label = nullptr;
static lv_obj_t* pet_label = nullptr;
static lv_obj_t* skill_panel = nullptr;
static lv_obj_t* node_btns[game::SKILL_N];
static lv_obj_t* node_sub[game::SKILL_N];
static char node_buf[game::SKILL_N][24];
static lv_obj_t* gauge_bars[beat::ALLY_N];
static bool skill_open = false;

/* HUD ラベル用の静的テキストバッファ (毎秒の malloc/free をなくす) */
static char buf_crystals[32], buf_next[48], buf_pet[24], buf_wave[24];

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

/* なでなでポップアップは fb 直接描画 (beat::flash_message) */
static uint32_t frame_count = 0;static int64_t fps_t0 = 0, last_tick_t = 0;
static uint32_t fps_value = 0;
static uint32_t hud_div = 0, save_div = 0, report_div = 0;
/* プロファイリング (us) */
static int64_t prof_step = 0, prof_render = 0;
static uint32_t prof_frames = 0;

static char fmt_buf[2][24];

/* ================= イベント ================= */
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
        beat::request_full_restore();   /* タッチによる LVGL 再描画を次フレームで修復 */
    } else if (code == LV_EVENT_PRESSING && sw_active) {
        sw_dx = p.x - sw_x0; sw_dy = p.y - sw_y0;
    } else if (code == LV_EVENT_RELEASED && sw_active) {
        sw_active = false;
        beat::request_full_restore();
        int64_t dt = (esp_timer_get_time() - sw_t0) / 1000;
        if (dt < 2000 && abs(sw_dy) > 60 && abs(sw_dy) > 2 * abs(sw_dx)) {
            game::pet_trigger();
            char msg[24];
            snprintf(msg, sizeof(msg), "PET! +%d%%", (int)gp::g.pet_buff_pct);
            beat::flash_message(msg, 45);
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
    if (lv_obj_has_flag(skill_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
        skill_open = true;
    } else {
        lv_obj_add_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
        skill_open = false;
    }
    beat::request_full_restore();   /* パネル開閉で壊れた領域を修復 */
}

static void close_skill_cb(lv_event_t* e)
{
    (void)e;
    lv_obj_add_flag(skill_panel, LV_OBJ_FLAG_HIDDEN);
    skill_open = false;
    beat::request_full_restore();
}

/* ================= パネル更新 (1Hz) ================= */
static void refresh_skill_panel()
{
    for (int i = 0; i < game::SKILL_N; i++) {
        lv_obj_t* sub = node_sub[i];
        if (!sub) continue;
        if (game::st.owned[i]) {
            snprintf(node_buf[i], 24, "OWNED");
            lv_obj_set_style_text_color(sub, lv_color_hex(0x8FE8A0), 0);
            lv_obj_set_style_bg_color(node_btns[i], lv_color_hex(0x1E5A34), 0);
        } else if (game::skill_locked(i)) {
            snprintf(node_buf[i], 24, "LOCKED");
            lv_obj_set_style_text_color(sub, lv_color_hex(0x888898), 0);
            lv_obj_set_style_bg_color(node_btns[i], lv_color_hex(0x282834), 0);
        } else {
            bool ok = game::skill_affordable(i);
            snprintf(node_buf[i], 24, "%s %s", game::fmt(game::SKILLS[i].cost, fmt_buf[0], 24),
                     ok ? "" : "(low)");
            lv_obj_set_style_text_color(sub, lv_color_hex(ok ? 0xFFFFFF : 0xE07070), 0);
            lv_obj_set_style_bg_color(node_btns[i], lv_color_hex(0x1C3A50), 0);
        }
        lv_label_set_text_static(sub, node_buf[i]);
    }
}

static void refresh_hud()
{
    /* HP バー (LUMI) */
    float hp = beat::ally_hp(0), hpmax = beat::ally_hpmax(0);
    if (hpmax > 0) lv_bar_set_value(hp_bar, hp / hpmax * 100.0f, LV_ANIM_OFF);

    /* 必殺技ゲージ (#023: 目の前に吊るす) */
    for (int i = 0; i < beat::ALLY_N; i++)
        lv_bar_set_value(gauge_bars[i], beat::ally_rage(i), LV_ANIM_OFF);

    /* 静的バッファに書いてから static text として設定 (アロケーションゼロ) */
    snprintf(buf_crystals, sizeof(buf_crystals), "C %s  (+%.1f/s)",
             game::fmt(game::st.crystals, fmt_buf[0], 24), game::income_rate());
    lv_label_set_text_static(crystals_label, buf_crystals);

    snprintf(buf_wave, sizeof(buf_wave), "WAVE %u", (unsigned)beat::wave());
    lv_label_set_text_static(wave_label, buf_wave);

    double diff = game::next_goal(fmt_buf[0], 24);
    snprintf(buf_next, sizeof(buf_next), "NEXT: %s (-%s)", fmt_buf[0],
             game::fmt(diff, fmt_buf[1], 24));
    lv_label_set_text_static(next_label, buf_next);

    if (game::pet_active()) {
        snprintf(buf_pet, sizeof(buf_pet), "PET %d:%02d",
                 game::pet_remaining_s() / 60, game::pet_remaining_s() % 60);
        lv_label_set_text_static(pet_label, buf_pet);
        lv_obj_clear_flag(pet_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(pet_label, LV_OBJ_FLAG_HIDDEN);
    }

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
    if (pend_heal) { pend_heal = false; beat::heal_all(); }
    if (pend_save) { pend_save = false; game::save(); Serial.println("[game] saved"); }
    if (pend_wave) {
        beat::set_wave(pend_wave);
        game::st.wave_reached = pend_wave;
        pend_wave = 0;
    }

    /* ゲーム進行 (スキルパネル表示中は描画を休止: パネルの絵を壊さない。戦闘は継続 = 放置) */
    int64_t t0 = esp_timer_get_time();
    game::tick(dt);
    beat::step();
    int64_t t1 = esp_timer_get_time();
    if (!skill_open) beat::render(game_fb + 56);
    int64_t t2 = esp_timer_get_time();
    prof_step += (t1 - t0);
    prof_render += (t2 - t1);
    prof_frames++;

    /* FPS */
    frame_count++;
    if (fps_t0 == 0) fps_t0 = now;
    if (now - fps_t0 >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / (now - fps_t0));
        frame_count = 0; fps_t0 = now;
    }

    /* HUD (1s @60fps) */
    if (++hud_div >= 60) { hud_div = 0; refresh_hud(); }

    /* オートセーブ (30s) */
    if (++save_div >= 1800) { save_div = 0; game::save(); }

    /* シリアルステータス (実時間 10s) */
    static int64_t last_report_t = 0;
    if (last_report_t == 0) last_report_t = now;
    if (now - last_report_t >= 10000000LL) {
        last_report_t = now;
        game::fmt(game::st.crystals, fmt_buf[0], 24);
        Serial.printf("[beat] fps=%u wave=%u crystals=%s lumiHP=%.0f rage=%.0f/%.0f/%.0f\r\n",
                      (unsigned)fps_value, (unsigned)beat::wave(), fmt_buf[0],
                      beat::ally_hp(0), beat::ally_rage(0), beat::ally_rage(1), beat::ally_rage(2));
        Serial.printf("[prof] step=%ldus render=%ldus fps=%ld\r\n",
                      (long)(prof_step / (prof_frames ? prof_frames : 1)),
                      (long)(prof_render / (prof_frames ? prof_frames : 1)),
                      (long)fps_value);
        Serial.printf("[mem] int_free=%u int_max=%u psram_free=%u\r\n",
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        prof_step = prof_render = 0;
        prof_frames = 0;
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

    /* --- ゲームはパネルのフレームバッファへ直接描画 (ゼロコピー) --- */
    game_fb = (uint16_t*)g_lcd->getFrameBufferByIndex(0);
    assert(game_fb);
    canvas_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(canvas_obj);
    lv_obj_set_pos(canvas_obj, 0, 56);
    lv_obj_set_size(canvas_obj, 480, 352);
    lv_obj_clear_flag(canvas_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(canvas_obj, canvas_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(canvas_obj, canvas_swipe_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas_obj, canvas_swipe_cb, LV_EVENT_RELEASED, NULL);

    /* --- 操作 UI なし (オートバトル観戦)。下部は必殺ゲージ表示 --- */
    make_label(scr, "SPECIAL GAUGES", 8, 412, 8, 0x8090A8);
    for (int i = 0; i < beat::ALLY_N; i++) {
        lv_obj_t* gb = lv_bar_create(scr);
        lv_obj_set_pos(gb, 8 + i * 150, 424);
        lv_obj_set_size(gb, 140, 22);
        lv_bar_set_range(gb, 0, 100);
        lv_bar_set_value(gb, 0, LV_ANIM_OFF);
        uint32_t col = (i == 0) ? 0x70D8E8 : (i == 1) ? 0x9C6CD8 : 0xD8A850;
        lv_obj_set_style_bg_color(gb, lv_color_hex(0x181820), 0);
        lv_obj_set_style_bg_color(gb, lv_color_hex(col), LV_PART_INDICATOR);
        gauge_bars[i] = gb;
    }
    make_label(scr, "LUMI", 10, 450, 8, 0x70D8E8);
    make_label(scr, "MIRA", 160, 450, 8, 0x9C6CD8);
    make_label(scr, "GORO", 310, 450, 8, 0xD8A850);

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

    /* なでなでポップアップとカットインは fb 直接描画 (beat 側) */

    /* --- フレームタイマ (60fps キャップ, 重いときは自動で落ちる) --- */
    lv_timer_create(tick_cb, 16, NULL);
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
    if (strcmp(line, "dump") == 0)  { beat::dump(); return; }
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
    g_lcd = board->getLCD();

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
