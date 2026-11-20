/**
 * app_io_workbench.cpp — ESP32-4848S040C Multi-Peripheral I/O Workbench Cartridge
 *
 * Genuine ESP32-S3 C++ firmware targeting hal/hw_hal.h + hal/hw_app.h.
 * Drives:
 *   - 3ch Relay Loads on J3 (GPIO 1 = Lamp, GPIO 2 = Fan, GPIO 40 = Beacon)
 *     honoring the physical R25-27 (RLY) vs R21-23 (I2S) solder-bridge jumper
 *   - 16-LED WS2812B NeoPixel Ring (GPIO 48 / RMT)
 *   - EC11 Detented Rotary Encoder + Push-Switch + Companion Tactile Button (GPIO 47)
 *   - 480x480 RGB565 GT911 Touch Control Surface
 */
#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hw_hal.h"
#include "hal/hw_app.h"
#include "sfx.h"
#include "font5x7.inc"

namespace {

static constexpr int W = 480;
static constexpr int H = 480;
static constexpr int LED_COUNT = 16;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void fill_rect(uint16_t* fb, int x, int y, int w, int h, uint16_t col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; ++r) {
        uint16_t* row = fb + (y + r) * W + x;
        for (int c = 0; c < w; ++c) row[c] = col;
    }
}

static void stroke_rect(uint16_t* fb, int x, int y, int w, int h, uint16_t col, int t = 2)
{
    fill_rect(fb, x, y, w, t, col);
    fill_rect(fb, x, y + h - t, w, t, col);
    fill_rect(fb, x, y, t, h, col);
    fill_rect(fb, x + w - t, y, t, h, col);
}

static void fill_circle(uint16_t* fb, int cx, int cy, int rad, uint16_t col)
{
    int r2 = rad * rad;
    for (int dy = -rad; dy <= rad; ++dy) {
        int y = cy + dy;
        if (y < 0 || y >= H) continue;
        for (int dx = -rad; dx <= rad; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                int x = cx + dx;
                if (x >= 0 && x < W) fb[y * W + x] = col;
            }
        }
    }
}

static void draw_text(uint16_t* fb, int x, int y, const char* s, uint16_t col, int scale = 2)
{
    int cx = x;
    while (*s) {
        char ch = *s++;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (ch >= 0x20 && ch <= 0x5F) {
            const uint8_t* g = FONT5X7[ch - 0x20];
            for (int r = 0; r < 7; ++r) {
                uint8_t bits = g[r];
                for (int c = 0; c < 5; ++c) {
                    if (bits & (0x10 >> c)) {
                        fill_rect(fb, cx + c * scale, y + r * scale, scale, scale, col);
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}

static void hsv_to_rgb(float h_deg, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    float h = fmodf(h_deg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0, gf = 0, bf = 0;
    if (h < 60)       { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    r = (uint8_t)((rf + m) * 255.0f + 0.5f);
    g = (uint8_t)((gf + m) * 255.0f + 0.5f);
    b = (uint8_t)((bf + m) * 255.0f + 0.5f);
}

/* ---------------- Workbench Firmware State ---------------- */
static bool    s_relay[3]       = { true, true, false }; // K1 Lamp, K2 Fan, K3 Beacon
static int     s_led_mode       = 0;  // 0=RAINBOW, 1=CHASE, 2=PULSE, 3=SOLID, 4=OFF
static int     s_enc_mode       = 0;  // 0=SPEED, 1=HUE, 2=BRIGHT
static float   s_led_speed      = 1.2f; // 0.1 .. 4.0
static float   s_led_hue        = 195.0f; // 0..360
static float   s_led_bright     = 0.90f;  // 0.1..1.0
static float   s_time           = 0.0f;
static int32_t s_enc_total      = 0;
static bool    s_enc_btn_prev   = false;
static bool    s_ext_btn_prev   = false;
static uint8_t s_ring_rgb[LED_COUNT][3];

static const char* kLedModeNames[5] = { "RAINBOW", "CHASE", "PULSE", "SOLID", "OFF" };
static const char* kEncModeNames[3] = { "SPEED", "HUE", "BRIGHT" };
static const int   kRelayPins[3]    = { 1, 2, 40 };

/* Touch UI Rects (all buttons >= 56px tall per UX Guideline) */
struct Rect { int x, y, w, h; };
static const Rect kRelayCard[3] = {
    {  14,  58, 144, 132 },
    { 168,  58, 144, 132 },
    { 322,  58, 144, 132 },
};
static const Rect kBtnLedMode = { 234, 212, 232, 54 };
static const Rect kBtnEncMode = { 234, 276, 232, 54 };
static const Rect kBtnAllOn   = { 234, 340, 112, 54 };
static const Rect kBtnAllOff  = { 354, 340, 112, 54 };

static bool hit(const Rect& r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void sync_relays()
{
    for (int i = 0; i < 3; ++i) {
        hw::gpio_write(kRelayPins[i], s_relay[i]);
    }
}

static void update_ring()
{
    for (int i = 0; i < LED_COUNT; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (s_led_mode == 0) {
            float hue = s_led_hue + (float)i * (360.0f / LED_COUNT) + s_time * 95.0f * s_led_speed;
            hsv_to_rgb(hue, 0.92f, s_led_bright, r, g, b);
        } else if (s_led_mode == 1) {
            float head = fmodf(s_time * 8.0f * s_led_speed, (float)LED_COUNT);
            float d = fabsf((float)i - head);
            if (d > LED_COUNT * 0.5f) d = LED_COUNT - d;
            float v = fmaxf(0.08f, 1.0f - d * 0.28f) * s_led_bright;
            hsv_to_rgb(s_led_hue + d * 18.0f, 0.95f, v, r, g, b);
        } else if (s_led_mode == 2) {
            float p = 0.20f + 0.80f * (0.5f + 0.5f * sinf(s_time * 3.8f * s_led_speed));
            hsv_to_rgb(s_led_hue, 0.88f, p * s_led_bright, r, g, b);
        } else if (s_led_mode == 3) {
            hsv_to_rgb(s_led_hue, 0.85f, s_led_bright, r, g, b);
        }
        s_ring_rgb[i][0] = r;
        s_ring_rgb[i][1] = g;
        s_ring_rgb[i][2] = b;
        hw::neopixel_set(48, i, r, g, b);
    }
    hw::neopixel_show(48);
}

static void apply_encoder_delta(int32_t d)
{
    if (d == 0) return;
    s_enc_total += d;
    if (s_enc_mode == 0) {
        s_led_speed += d * 0.15f;
        if (s_led_speed < 0.1f) s_led_speed = 0.1f;
        if (s_led_speed > 4.0f) s_led_speed = 4.0f;
    } else if (s_enc_mode == 1) {
        s_led_hue = fmodf(s_led_hue + d * 15.0f, 360.0f);
        if (s_led_hue < 0.0f) s_led_hue += 360.0f;
    } else {
        s_led_bright += d * 0.06f;
        if (s_led_bright < 0.10f) s_led_bright = 0.10f;
        if (s_led_bright > 1.00f) s_led_bright = 1.00f;
    }
}

} // anonymous namespace

namespace hw {

const AppInfo& app_info()
{
    static const AppInfo kInfo = {
        "io_workbench",
        "I/O Workbench",
        "esp32_4848s040c",
        W,
        H,
        false,
    };
    return kInfo;
}

void app_init()
{
    s_relay[0] = true;
    s_relay[1] = true;
    s_relay[2] = false;
    s_led_mode = 0;
    s_enc_mode = 0;
    s_led_speed = 1.2f;
    s_led_hue = 195.0f;
    s_led_bright = 0.90f;
    s_time = 0.0f;
    s_enc_total = 0;
    s_enc_btn_prev = false;
    s_ext_btn_prev = false;

    sfx::init();
    hw::gpio_mode(1, hw::PIN_OUTPUT);
    hw::gpio_mode(2, hw::PIN_OUTPUT);
    hw::gpio_mode(40, hw::PIN_OUTPUT);
    hw::gpio_mode(47, hw::PIN_INPUT);
    sync_relays();

    hw::neopixel_begin(48, LED_COUNT);
    update_ring();

    Serial.println("== ESP32-4848S040C Multi-Peripheral I/O Workbench ==");
    Serial.println("Peripherals: 3ch Relay (IO1/IO2/IO40) | WS2812B 16-LED (IO48) | EC11 Encoder | BTN-A (IO47)");
    Serial.println("Commands: relay <1|2|3> <0|1> | all <0|1> | led <0..4> | enc <delta> | status | help");
}

void app_step(float dt_sec)
{
    s_time += dt_sec;

    /* Poll 3D / Hardware EC11 Rotary Encoder */
    int32_t d = hw::encoder_consume_delta(0);
    if (d != 0) apply_encoder_delta(d);

    bool encBtn = hw::encoder_button(0);
    if (encBtn && !s_enc_btn_prev) {
        s_enc_mode = (s_enc_mode + 1) % 3;
        sfx::play(sfx::SFX_TAP);
        Serial.printf("[io] Encoder mode -> %s\n", kEncModeNames[s_enc_mode]);
    }
    s_enc_btn_prev = encBtn;

    /* Poll Companion Red Tactile Switch (GPIO 47) -> toggles CH3 Beacon */
    bool extBtn = hw::gpio_read(47);
    if (extBtn && !s_ext_btn_prev) {
        s_relay[2] = !s_relay[2];
        sync_relays();
        sfx::play(sfx::SFX_CONFIRM);
        Serial.printf("[io] BTN-A (GPIO47) -> CH3 BEACON %s\n", s_relay[2] ? "ON" : "OFF");
    }
    s_ext_btn_prev = extBtn;

    update_ring();
}

void app_render(uint16_t* fb, int buf_idx)
{
    (void)buf_idx;
    const uint16_t cBg     = rgb565(11, 15, 24);
    const uint16_t cPanel  = rgb565(18, 25, 38);
    const uint16_t cBorder = rgb565(52, 84, 118);
    const uint16_t cAccent = rgb565(112, 226, 244);
    const uint16_t cGold   = rgb565(250, 198, 78);
    const uint16_t cGreen  = rgb565(78, 240, 154);
    const uint16_t cAmber  = rgb565(255, 148, 36);
    const uint16_t cText   = rgb565(234, 244, 255);
    const uint16_t cMuted  = rgb565(132, 154, 184);

    fill_rect(fb, 0, 0, W, H, cBg);

    /* Header Bar */
    fill_rect(fb, 0, 0, W, 46, rgb565(15, 22, 36));
    fill_rect(fb, 0, 44, W, 2, cAccent);
    draw_text(fb, 14, 15, "I/O WORKBENCH", cAccent, 2);

    bool rlyRouted = hw::jumper_state(hw::JUMPER_AUDIO_RELAY) != 0;
    uint16_t jCol = rlyRouted ? cGreen : cGold;
    fill_rect(fb, 304, 8, 162, 28, rgb565(22, 32, 48));
    stroke_rect(fb, 304, 8, 162, 28, jCol, 2);
    draw_text(fb, 314, 15, rlyRouted ? "R25-27 RLY" : "R21-23 I2S", jCol, 2);

    /* 3 Relay Load Cards */
    const char* titles[3] = { "K1 LAMP", "K2 FAN", "K3 BEACON" };
    const char* pins[3]   = { "IO1", "IO2", "IO40" };
    const uint16_t onCols[3] = { cGold, cAccent, cAmber };

    for (int i = 0; i < 3; ++i) {
        const Rect& r = kRelayCard[i];
        bool active = s_relay[i] && rlyRouted;
        uint16_t bg = active ? rgb565(26, 40, 56) : cPanel;
        uint16_t br = active ? onCols[i] : cBorder;
        fill_rect(fb, r.x, r.y, r.w, r.h, bg);
        stroke_rect(fb, r.x, r.y, r.w, r.h, br, 2);

        draw_text(fb, r.x + 12, r.y + 10, titles[i], active ? onCols[i] : cText, 2);
        draw_text(fb, r.x + 12, r.y + 32, pins[i], cMuted, 2);

        /* Crisp visual load indicator icon */
        int icx = r.x + r.w - 34;
        int icy = r.y + 68;
        if (i == 0) {
            fill_circle(fb, icx, icy, 18, active ? cGold : rgb565(42, 48, 60));
            fill_circle(fb, icx, icy, 9, active ? rgb565(255, 248, 210) : rgb565(24, 30, 42));
        } else if (i == 1) {
            fill_circle(fb, icx, icy, 19, rgb565(28, 38, 54));
            float a0 = active ? s_time * 11.0f : 0.0f;
            for (int b = 0; b < 4; ++b) {
                float a = a0 + b * 1.5708f;
                int bx = icx + (int)(cosf(a) * 11.0f);
                int by = icy + (int)(sinf(a) * 11.0f);
                fill_circle(fb, bx, by, 5, active ? cAccent : cMuted);
            }
            fill_circle(fb, icx, icy, 5, cText);
        } else {
            bool flash = active && (sinf(s_time * 9.0f) > 0.0f);
            fill_circle(fb, icx, icy, 18, flash ? cAmber : (active ? rgb565(170, 82, 18) : rgb565(42, 48, 60)));
            fill_circle(fb, icx, icy, 8, flash ? rgb565(255, 240, 180) : rgb565(26, 32, 44));
        }

        const char* stLbl = !rlyRouted ? "I2S LOCK" : (s_relay[i] ? "ON  5V" : "OFF 0V");
        uint16_t stCol = !rlyRouted ? cGold : (s_relay[i] ? cGreen : cMuted);
        draw_text(fb, r.x + 12, r.y + 102, stLbl, stCol, 2);
    }

    /* WS2812B 16-LED Ring Live Mirror Panel (Left) */
    fill_rect(fb, 14, 202, 208, 202, cPanel);
    stroke_rect(fb, 14, 202, 208, 202, cBorder, 2);
    draw_text(fb, 26, 212, "WS2812B IO48", cAccent, 2);

    int ringCx = 118, ringCy = 310, ringR = 64;
    for (int i = 0; i < LED_COUNT; ++i) {
        float ang = -1.5708f + (float)i * (6.283185f / LED_COUNT);
        int lx = ringCx + (int)(cosf(ang) * ringR);
        int ly = ringCy + (int)(sinf(ang) * ringR);
        uint16_t lc = rgb565(s_ring_rgb[i][0], s_ring_rgb[i][1], s_ring_rgb[i][2]);
        fill_circle(fb, lx, ly, 10, rgb565(34, 42, 56));
        fill_circle(fb, lx, ly, 7, lc);
    }
    draw_text(fb, ringCx - 36, ringCy - 7, kLedModeNames[s_led_mode], cText, 2);

    /* Right Control Buttons */
    auto drawBtn = [&](const Rect& r, const char* l1, const char* l2, uint16_t br, uint16_t col1, uint16_t col2) {
        fill_rect(fb, r.x, r.y, r.w, r.h, cPanel);
        stroke_rect(fb, r.x, r.y, r.w, r.h, br, 2);
        draw_text(fb, r.x + 12, r.y + 8, l1, col1, 2);
        if (l2 && *l2) draw_text(fb, r.x + 12, r.y + 30, l2, col2, 2);
    };

    char encVal[24];
    if (s_enc_mode == 0) snprintf(encVal, sizeof(encVal), "SPD %.1fX", s_led_speed);
    else if (s_enc_mode == 1) snprintf(encVal, sizeof(encVal), "HUE %03d", (int)s_led_hue);
    else snprintf(encVal, sizeof(encVal), "BRT %d%%", (int)(s_led_bright * 100.0f));

    drawBtn(kBtnLedMode, "LED PATTERN", kLedModeNames[s_led_mode], cAccent, cMuted, cAccent);
    drawBtn(kBtnEncMode, "EC11 KNOB", encVal, cGold, cMuted, cGold);
    drawBtn(kBtnAllOn,   "ALL ON",  "K1-K3", cGreen, cGreen, cMuted);
    drawBtn(kBtnAllOff,  "ALL OFF", "RESET", cBorder, cText, cMuted);

    /* Footer Live Bus Strip */
    fill_rect(fb, 14, 416, 452, 48, rgb565(15, 21, 34));
    stroke_rect(fb, 14, 416, 452, 48, cBorder, 2);
    char foot[48];
    snprintf(foot, sizeof(foot), "IO1:%d IO2:%d IO40:%d  ENC:%ld",
             s_relay[0] ? 1 : 0, s_relay[1] ? 1 : 0, s_relay[2] ? 1 : 0, (long)s_enc_total);
    draw_text(fb, 26, 432, foot, cAccent, 2);
}

void app_on_touch(int event_type, int x, int y)
{
    if (event_type != TOUCH_DOWN) return;

    for (int i = 0; i < 3; ++i) {
        if (hit(kRelayCard[i], x, y)) {
            s_relay[i] = !s_relay[i];
            sync_relays();
            sfx::play(sfx::SFX_TAP);
            Serial.printf("[io] Relay K%d (GPIO%d) -> %s\n", i + 1, kRelayPins[i], s_relay[i] ? "ON" : "OFF");
            return;
        }
    }
    if (hit(kBtnLedMode, x, y)) {
        s_led_mode = (s_led_mode + 1) % 5;
        sfx::play(sfx::SFX_CONFIRM);
        Serial.printf("[io] WS2812B pattern -> %s\n", kLedModeNames[s_led_mode]);
        return;
    }
    if (hit(kBtnEncMode, x, y)) {
        s_enc_mode = (s_enc_mode + 1) % 3;
        sfx::play(sfx::SFX_TAP);
        Serial.printf("[io] Encoder target -> %s\n", kEncModeNames[s_enc_mode]);
        return;
    }
    if (hit(kBtnAllOn, x, y)) {
        s_relay[0] = s_relay[1] = s_relay[2] = true;
        sync_relays();
        sfx::play(sfx::SFX_CONFIRM);
        Serial.println("[io] All relays ON");
        return;
    }
    if (hit(kBtnAllOff, x, y)) {
        s_relay[0] = s_relay[1] = s_relay[2] = false;
        sync_relays();
        sfx::play(sfx::SFX_CANCEL);
        Serial.println("[io] All relays OFF");
        return;
    }
}

void app_on_uart(const char* line)
{
    if (!line) return;
    while (*line == ' ') line++;
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        Serial.println("[io] relay <1|2|3> <0|1> | all <0|1> | led <0..4> | enc <delta> | status");
        return;
    }
    if (strcmp(line, "status") == 0) {
        Serial.printf("[io] K1=%d K2=%d K3=%d LED=%s ENC=%ld(%s) JUMPER=%s\n",
                      s_relay[0] ? 1 : 0, s_relay[1] ? 1 : 0, s_relay[2] ? 1 : 0,
                      kLedModeNames[s_led_mode], (long)s_enc_total, kEncModeNames[s_enc_mode],
                      hw::jumper_state(hw::JUMPER_AUDIO_RELAY) ? "R25-27:RLY" : "R21-23:I2S");
        return;
    }
    if (strncmp(line, "relay ", 6) == 0) {
        int ch = 0, val = 0;
        if (sscanf(line + 6, "%d %d", &ch, &val) == 2 && ch >= 1 && ch <= 3) {
            s_relay[ch - 1] = (val != 0);
            sync_relays();
            Serial.printf("[io] K%d -> %d\n", ch, val ? 1 : 0);
            return;
        }
    }
    if (strncmp(line, "all ", 4) == 0) {
        bool v = atoi(line + 4) != 0;
        s_relay[0] = s_relay[1] = s_relay[2] = v;
        sync_relays();
        Serial.printf("[io] All relays -> %d\n", v ? 1 : 0);
        return;
    }
    if (strncmp(line, "led ", 4) == 0) {
        int m = atoi(line + 4);
        if (m >= 0 && m < 5) {
            s_led_mode = m;
            Serial.printf("[io] LED -> %s\n", kLedModeNames[s_led_mode]);
            return;
        }
    }
    if (strncmp(line, "enc ", 4) == 0) {
        apply_encoder_delta(atoi(line + 4));
        Serial.printf("[io] Encoder -> %ld\n", (long)s_enc_total);
        return;
    }
    Serial.printf("[io] unknown cmd: %s\n", line);
}

void app_on_save() {}

void app_on_status_log(uint32_t fps)
{
    Serial.printf("[io] fps=%u K1=%d K2=%d K3=%d LED=%s\r\n",
                  (unsigned)fps, s_relay[0] ? 1 : 0, s_relay[1] ? 1 : 0, s_relay[2] ? 1 : 0,
                  kLedModeNames[s_led_mode]);
}

void app_audio_set_rate(uint32_t hz) { sfx::set_sample_rate(hz); }
void app_audio_render(float* out_f32, int frames) { sfx::render_f32(out_f32, frames); }
void app_audio_set_muted(bool muted) { sfx::set_muted(muted); }
bool app_audio_is_muted() { return sfx::is_muted(); }

} // namespace hw
