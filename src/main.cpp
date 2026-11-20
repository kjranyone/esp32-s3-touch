/**
 * main.cpp — Multi-Board Real-Hardware Runtime Host for ESP32-S3 Cartridges
 *
 * Drives any firmware cartridge implementing hal/hw_app.h on either:
 *   - BOARD_JINGCAI_ESP32_4848S040C_I_Y_3 (ST7701 480x480 RGB565 DMA + GT911 touch)
 *   - BOARD_ESP32_S3_DEVKITC              (ESP32-S3-DevKitC-1 + I2C OLED / UART / GPIO)
 *
 * Zero app-specific code lives here.
 */
#ifndef __EMSCRIPTEN__

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>
#include <stdlib.h>

#include "hal/hw_hal.h"
#include "hal/hw_app.h"

#ifdef BOARD_ESP32_S3_DEVKITC

/* ============================================================
 * ESP32-S3-DevKitC-1 Generic Runtime Host (OLED / Headless / I/O)
 * ============================================================ */
static uint16_t* s_fb = nullptr;
static int64_t   last_frame_t = 0;
static uint32_t  frame_count = 0;
static int64_t   fps_t0 = 0;
static uint32_t  fps_value = 0;
static int64_t   last_status_t = 0;
static char      cmd_buf[128];
static size_t    cmd_len = 0;

static void pump_serial()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            cmd_buf[cmd_len] = 0;
            char* s = cmd_buf;
            while (*s == ' ') s++;
            if (*s != 0) hw::app_on_uart(s);
            cmd_len = 0;
        } else if (cmd_len < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_len++] = c;
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.printf("\r\n== ESP32-S3-DevKitC Host | Cartridge: %s (%s) ==\r\n",
                  hw::app_info().name, hw::app_info().id);
    const size_t px = (size_t)hw::app_info().display_w * (size_t)hw::app_info().display_h;
    s_fb = (uint16_t*)calloc(px, sizeof(uint16_t));
    hw::app_init();
    if (s_fb) hw::app_render(s_fb, 0);
    Serial.println("Setup done");
}

void loop()
{
    pump_serial();
    int64_t now = esp_timer_get_time();
    if (last_frame_t != 0 && now - last_frame_t < 16000) {
        delay(1);
        return;
    }
    float dt = (last_frame_t == 0) ? 0.016f : (now - last_frame_t) / 1000000.0f;
    last_frame_t = now;
    if (dt > 0.1f) dt = 0.1f;

    hw::app_step(dt);
    if (s_fb) hw::app_render(s_fb, 0);

    frame_count++;
    if (fps_t0 == 0) fps_t0 = now;
    if (now - fps_t0 >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / (now - fps_t0));
        frame_count = 0;
        fps_t0 = now;
    }
    if (now - last_status_t >= 10000000LL) {
        last_status_t = now;
        hw::app_on_status_log(fps_value);
    }
}

#else

/* ============================================================
 * JingCai ESP32-4848S040C_I_Y_3 RGB565 + GT911 Runtime Host
 * ============================================================ */
#include <esp_display_panel.hpp>
#include <esp_cache.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <assert.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static Board*            g_board     = nullptr;
static LCD*              g_lcd       = nullptr;
static Touch*            g_touch     = nullptr;
static uint16_t*         canvas[2]   = {nullptr, nullptr};
static int               render_idx  = 0;
static int               scan_idx    = 0;
static SemaphoreHandle_t fb_done_sem = nullptr;

IRAM_ATTR static bool on_refresh_finish(void*)
{
    BaseType_t hpw = pdFALSE;
    if (fb_done_sem != nullptr) {
        xSemaphoreGiveFromISR(fb_done_sem, &hpw);
    }
    return hpw == pdTRUE;
}

static int64_t  last_frame_t  = 0;
static uint32_t frame_count   = 0;
static int64_t  fps_t0        = 0;
static uint32_t fps_value     = 0;
static int64_t  last_status_t = 0;
static int64_t  prof_render   = 0;
static uint32_t prof_frames   = 0;
static volatile bool shot_req     = false;
static volatile bool pause_render = false;

static bool touch_down_prev = false;
static int  touch_last_x    = 0;
static int  touch_last_y    = 0;

static void poll_touch()
{
    if (!g_touch) return;
    TouchPoint pts[2];
    int n = g_touch->readPoints(pts, 2);

    bool pressed = (n > 0);
    if (pressed) {
        touch_last_x = pts[0].x;
        touch_last_y = pts[0].y;
    }

    if (pressed && !touch_down_prev) {
        hw::app_on_touch(hw::TOUCH_DOWN, touch_last_x, touch_last_y);
    } else if (pressed && touch_down_prev) {
        hw::app_on_touch(hw::TOUCH_MOVE, touch_last_x, touch_last_y);
    } else if (!pressed && touch_down_prev) {
        hw::app_on_touch(hw::TOUCH_UP, touch_last_x, touch_last_y);
    }
    touch_down_prev = pressed;
}

static char   cmd_buf[128];
static size_t cmd_len = 0;

static void do_shot()
{
    pause_render = true;
    delay(80);
    const int w = hw::app_info().display_w;
    const int h = hw::app_info().display_h;
    Serial.printf("SHOT %d %d\n", w, h);
    static uint8_t sbuf[512];
    int bl = 0;
    uint32_t total = (uint32_t)w * (uint32_t)h;
    uint32_t i = 0;
    uint16_t* p = canvas[scan_idx];
    while (i < total) {
        uint16_t c = p[i];
        uint32_t run = 1;
        while (i + run < total && p[i + run] == c && run < 65535) run++;
        sbuf[bl++] = (uint8_t)(run & 0xFF);
        sbuf[bl++] = (uint8_t)((run >> 8) & 0xFF);
        sbuf[bl++] = (uint8_t)(c & 0xFF);
        sbuf[bl++] = (uint8_t)(c >> 8);
        i += run;
        if (bl >= 508) { Serial.write(sbuf, bl); bl = 0; }
    }
    if (bl) Serial.write(sbuf, bl);
    Serial.print("\nRLEEND\n");
    pause_render = false;
}

static void pump_serial()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            cmd_buf[cmd_len] = 0;
            char* s = cmd_buf;
            while (*s == ' ') s++;
            if (strcmp(s, "shot") == 0) {
                shot_req = true;
            } else if (*s != 0) {
                hw::app_on_uart(s);
            }
            cmd_len = 0;
        } else if (cmd_len < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_len++] = c;
        }
    }
}

static void frame()
{
    int64_t now = esp_timer_get_time();
    float dt = (last_frame_t == 0) ? 0.016f : (now - last_frame_t) / 1000000.0f;
    last_frame_t = now;
    if (dt > 0.1f) dt = 0.1f;

    if (pause_render) return;
    if (shot_req) { shot_req = false; do_shot(); }

    poll_touch();
    hw::app_step(dt);

    bool can_draw = (xSemaphoreTake(fb_done_sem, pdMS_TO_TICKS(30)) == pdTRUE);
    if (!can_draw) return;

    uint16_t* target = canvas[render_idx];
    int64_t t0 = esp_timer_get_time();
    hw::app_render(target, render_idx);
    prof_render += esp_timer_get_time() - t0;
    prof_frames++;

    esp_cache_msync((void*)target, 480 * 480 * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    while (xSemaphoreTake(fb_done_sem, 0) == pdTRUE) {}
    g_lcd->switchFrameBufferTo(target);
    while (xSemaphoreTake(fb_done_sem, 0) == pdTRUE) {}
    scan_idx = render_idx;
    render_idx ^= 1;

    frame_count++;
    if (fps_t0 == 0) fps_t0 = now;
    if (now - fps_t0 >= 1000000LL) {
        fps_value = (uint32_t)(frame_count * 1000000LL / (now - fps_t0));
        frame_count = 0;
        fps_t0 = now;
    }

    if (now - last_status_t >= 10000000LL) {
        last_status_t = now;
        hw::app_on_status_log(fps_value);
        Serial.printf("[host] app=%s render=%ldus fps=%u int_free=%u psram_free=%u\r\n",
                      hw::app_info().id,
                      (long)(prof_render / (prof_frames ? prof_frames : 1)),
                      (unsigned)fps_value,
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        prof_render = 0;
        prof_frames = 0;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.printf("== ESP32-S3 Workbench Host | Cartridge: %s (%s) ==\r\n",
                  hw::app_info().name, hw::app_info().id);
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

    hw::app_init();

    hw::app_render(canvas[1], 1);
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

#endif // BOARD_ESP32_S3_DEVKITC
#endif // !__EMSCRIPTEN__
