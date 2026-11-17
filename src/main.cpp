/**
 * 『ルミの水晶庭園』Phase 1: 物理箱庭エンジンの動作確認
 *
 * - 落下砂モデル (水 / 砂 / 岩 / 結晶の種 / 結晶) を 240x172 グリッドでシミュレート
 * - LVGL キャンバスに 30fps で描画
 * - タッチで水・砂・岩を注ぐ / 消しゴム / リセット
 *
 * 操作: 下部ボタンで素材を選び、キャンバスをなぞる
 */
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "sandbox.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* ---------------- UI ハンドル ---------------- */
static lv_obj_t* canvas_obj = nullptr;
static uint16_t* canvas_buf = nullptr;
static lv_obj_t* fps_label = nullptr;
static lv_obj_t* cnt_label = nullptr;
static lv_obj_t* mat_btns[4] = {nullptr, nullptr, nullptr, nullptr};
static sandbox::Mat selected = sandbox::WATER;

/* ---------------- フレーム統計 ---------------- */
static uint32_t frame_count = 0;
static int64_t fps_t0 = 0;
static uint32_t fps_value = 0;
static uint32_t status_div = 0;

static const char* MAT_NAMES[4] = {"WATER", "SAND", "ROCK", "ERASE"};

/* ---------------- タッチで素材を注ぐ ---------------- */
static void canvas_press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int gx = p.x / sandbox::CELL;
    int gy = (p.y - 64) / sandbox::CELL;
    if (gx < 0 || gx >= sandbox::GW || gy < 0 || gy >= sandbox::GH) return;

    sandbox::Mat m = selected;
    if (m == sandbox::ROCK) {
        /* 岩はタップ(短押し)で配置、なぞりでも連続配置可 */
    }
    sandbox::stamp(gx, gy, 3, m);
}

static void mat_btn_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    selected = (idx == 3) ? sandbox::EMPTY
             : (idx == 0) ? sandbox::WATER
             : (idx == 1) ? sandbox::SAND
                          : sandbox::ROCK;
    for (int i = 0; i < 4; i++) {
        if (mat_btns[i]) lv_obj_clear_state(mat_btns[i], LV_STATE_CHECKED);
    }
    lv_obj_add_state(btn, LV_STATE_CHECKED);
}

static void reset_btn_cb(lv_event_t* e)
{
    (void)e;
    sandbox::init();
    if (canvas_obj) lv_obj_invalidate(canvas_obj);
}

/* ---------------- 毎フレームの更新 ---------------- */
static void tick_cb(lv_timer_t* timer)
{
    (void)timer;

    sandbox::step();
    sandbox::render(canvas_buf);
    lv_obj_invalidate(canvas_obj);

    /* FPS 計測 (1秒ごと) */
    frame_count++;
    int64_t now = esp_timer_get_time();
    if (fps_t0 == 0) fps_t0 = now;
    int64_t dt = now - fps_t0;
    if (dt >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / dt);
        frame_count = 0;
        fps_t0 = now;
        if (fps_label) {
            lv_label_set_text_fmt(fps_label, "FPS %u", (unsigned)fps_value);
        }
    }

    /* 状態表示 (1秒ごと) */
    if (++status_div >= 30) {
        status_div = 0;
        if (cnt_label) {
            lv_label_set_text_fmt(cnt_label, "W:%d S:%d C:%d",
                                  sandbox::count(sandbox::WATER),
                                  sandbox::count(sandbox::SAND),
                                  sandbox::count(sandbox::CRYSTAL) + sandbox::count(sandbox::SEED));
        }
        static uint32_t report = 0;
        if (++report % 5 == 0) {
            Serial.printf("[sandbox] fps=%u water=%d sand=%d crystal=%d seed=%d\r\n",
                          (unsigned)fps_value,
                          sandbox::count(sandbox::WATER),
                          sandbox::count(sandbox::SAND),
                          sandbox::count(sandbox::CRYSTAL),
                          sandbox::count(sandbox::SEED));
        }
    }
}

/* ---------------- UI 構築 ---------------- */
static lv_obj_t* make_btn(lv_obj_t* parent, const char* text, int idx)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 84, 52);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, mat_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    lv_obj_t* l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return btn;
}

static void build_ui()
{
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101018), 0);

    /* --- HUD (y 0..63) --- */
    lv_obj_t* hud = lv_obj_create(scr);
    lv_obj_remove_style_all(hud);
    lv_obj_set_pos(hud, 0, 0);
    lv_obj_set_size(hud, 480, 64);

    lv_obj_t* title = lv_label_create(hud);
    lv_label_set_text(title, "Crystal Garden - P1");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x9FF8E8), 0);
    lv_obj_set_pos(title, 8, 6);

    fps_label = lv_label_create(hud);
    lv_label_set_text(fps_label, "FPS --");
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0xB0B0C0), 0);
    lv_obj_set_pos(fps_label, 380, 6);

    cnt_label = lv_label_create(hud);
    lv_label_set_text(cnt_label, "W:- S:- C:-");
    lv_obj_set_style_text_font(cnt_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cnt_label, lv_color_hex(0x88C0D0), 0);
    lv_obj_set_pos(cnt_label, 8, 36);

    /* --- 物理キャンバス (y 64..407) --- */
    canvas_buf = (uint16_t*)heap_caps_calloc(sandbox::FB_W * sandbox::FB_H,
                                             sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(canvas_buf);

    canvas_obj = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas_obj, canvas_buf,
                         sandbox::FB_W, sandbox::FB_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(canvas_obj, 0, 64);
    lv_obj_add_event_cb(canvas_obj, canvas_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_clear_flag(canvas_obj, LV_OBJ_FLAG_SCROLLABLE);

    /* --- 下部ボタンバー (y 408..479) --- */
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 0, 408);
    lv_obj_set_size(bar, 480, 72);

    lv_obj_t* b_water = make_btn(bar, MAT_NAMES[0], 0);
    lv_obj_t* b_sand  = make_btn(bar, MAT_NAMES[1], 1);
    lv_obj_t* b_rock  = make_btn(bar, MAT_NAMES[2], 2);
    lv_obj_t* b_erase = make_btn(bar, MAT_NAMES[3], 3);
    lv_obj_align(b_water, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_align_to(b_sand, b_water, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_align_to(b_rock, b_sand, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_align_to(b_erase, b_rock, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    mat_btns[0] = b_water; mat_btns[1] = b_sand; mat_btns[2] = b_rock; mat_btns[3] = b_erase;

    lv_obj_t* b_reset = lv_btn_create(bar);
    lv_obj_set_size(b_reset, 72, 52);
    lv_obj_add_event_cb(b_reset, reset_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* rl = lv_label_create(b_reset);
    lv_label_set_text(rl, LV_SYMBOL_TRASH);
    lv_obj_center(rl);
    lv_obj_align(b_reset, LV_ALIGN_RIGHT_MID, -10, 0);

    /* 初期選択: WATER */
    lv_obj_add_state(b_water, LV_STATE_CHECKED);

    /* --- 物理タイマ (約30fps) --- */
    lv_timer_create(tick_cb, 33, NULL);
}

/* ---------------- 起動 ---------------- */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("== Crystal Garden P1 (falling-sand sandbox) ==");
    Serial.printf("Chip %s rev%d, PSRAM %d MB, Flash %d MB\r\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getPsramSize() / 1024 / 1024,
                  ESP.getFlashChipSize() / 1024 / 1024);
    Serial.printf("mem: psramFound=%d SPIRAM total=%u free=%u / internal free=%u\r\n",
                  (int)psramFound(),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    Serial.println("Initializing board");
    Board* board = new Board();
    board->init();
    assert(board->begin());

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Building UI");
    lvgl_port_lock(-1);
    sandbox::init();
    build_ui();
    lvgl_port_unlock();

    Serial.println("Setup done");
}

void loop()
{
    delay(1000);
}
