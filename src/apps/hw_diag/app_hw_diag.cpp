/**
 * app_hw_diag.cpp — ESP32-4848S040C Hardware Diagnostics & Bus Test Cartridge
 *
 * A real ESP32-S3 firmware targeting hal/hw_hal.h + hal/hw_app.h.
 * Runs identically on physical ESP32-4848S040C hardware (pio run -e hw_diag)
 * and inside the Web 3D Hardware Workbench (WASM cartridge).
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

/* ---------------- Interactive Diagnostics State ---------------- */
static constexpr int TRAIL_MAX = 96;
struct TrailPt { int16_t x, y; uint8_t age; };
static TrailPt s_trail[TRAIL_MAX];
static int     s_trail_head = 0;

static bool    s_touch_down = false;
static int     s_touch_x    = 240;
static int     s_touch_y    = 148;
static float   s_time       = 0.0f;

static bool    s_io1        = false;
static bool    s_io2        = false;
static bool    s_io40       = false;
static int     s_led_mode   = 1; // 0=OFF, 1=RAINBOW, 2=PULSE
static int32_t s_enc_pos    = 0;
static int     s_tone_idx   = 0;

/* Touch Pad Canvas Region */
static constexpr int PAD_X = 16, PAD_Y = 58, PAD_W = 448, PAD_H = 196;

/* 6 Big Tactile Buttons (>= 56px tall per UX Guideline) */
struct Btn { int x, y, w, h; const char* label; };
static const Btn kBtns[6] = {
    {  16, 278, 142, 62, "IO1 RLY1" },
    { 169, 278, 142, 62, "IO2 RLY2" },
    { 322, 278, 142, 62, "IO40 RLY3" },
    {  16, 352, 142, 62, "LED MODE" },
    { 169, 352, 142, 62, "TONE SFX" },
    { 322, 352, 142, 62, "CLEAR"    },
};

static void sync_gpio_outputs()
{
    hw::gpio_write(1,  s_io1);
    hw::gpio_write(2,  s_io2);
    hw::gpio_write(40, s_io40);
}

static void add_trail(int x, int y)
{
    s_trail[s_trail_head] = { (int16_t)x, (int16_t)y, 255 };
    s_trail_head = (s_trail_head + 1) % TRAIL_MAX;
}

static void update_neopixels()
{
    if (s_led_mode == 0) {
        hw::neopixel_clear(48);
        hw::neopixel_show(48);
        return;
    }
    for (int i = 0; i < 16; ++i) {
        float phase = s_time * 2.4f + i * (6.283185f / 16.0f);
        uint8_t r, g, b;
        if (s_led_mode == 1) {
            r = (uint8_t)(127.5f + 127.5f * sinf(phase));
            g = (uint8_t)(127.5f + 127.5f * sinf(phase + 2.094f));
            b = (uint8_t)(127.5f + 127.5f * sinf(phase + 4.188f));
        } else {
            float pulse = 0.5f + 0.5f * sinf(s_time * 4.0f);
            r = (uint8_t)(28 * pulse);
            g = (uint8_t)(210 * pulse);
            b = (uint8_t)(255 * pulse);
        }
        hw::neopixel_set(48, i, r, g, b);
    }
    hw::neopixel_show(48);
}

static void press_button(int idx)
{
    if (idx == 0) {
        s_io1 = !s_io1;
        sync_gpio_outputs();
        sfx::play(sfx::SFX_TAP);
        Serial.printf("[diag] GPIO1 = %d\n", s_io1 ? 1 : 0);
    } else if (idx == 1) {
        s_io2 = !s_io2;
        sync_gpio_outputs();
        sfx::play(sfx::SFX_TAP);
        Serial.printf("[diag] GPIO2 = %d\n", s_io2 ? 1 : 0);
    } else if (idx == 2) {
        s_io40 = !s_io40;
        sync_gpio_outputs();
        sfx::play(sfx::SFX_TAP);
        Serial.printf("[diag] GPIO40 = %d\n", s_io40 ? 1 : 0);
    } else if (idx == 3) {
        s_led_mode = (s_led_mode + 1) % 3;
        sfx::play(sfx::SFX_CONFIRM);
        Serial.printf("[diag] WS2812 mode = %d\n", s_led_mode);
    } else if (idx == 4) {
        static const sfx::Id kTones[4] = {
            sfx::SFX_CONFIRM, sfx::SFX_CRYSTAL, sfx::SFX_WAVE_CLEAR, sfx::SFX_CHEST_OPEN
        };
        sfx::Id id = kTones[s_tone_idx % 4];
        s_tone_idx++;
        sfx::play(id);
        Serial.printf("[diag] SFX test: %s (jumper=%s)\n",
                      sfx::name(id),
                      hw::jumper_state(hw::JUMPER_AUDIO_RELAY) ? "R25-27:RLY" : "R21-23:I2S");
    } else if (idx == 5) {
        memset(s_trail, 0, sizeof(s_trail));
        s_io1 = s_io2 = s_io40 = false;
        sync_gpio_outputs();
        sfx::play(sfx::SFX_CANCEL);
        Serial.println("[diag] Cleared touch trail & reset GPIO 1/2/40");
    }
}

} // anonymous namespace

namespace hw {

const AppInfo& app_info()
{
    static const AppInfo kInfo = {
        "hw_diag",
        "HW Diagnostics",
        "esp32_4848s040c",
        W,
        H,
        false,
    };
    return kInfo;
}

void app_init()
{
    memset(s_trail, 0, sizeof(s_trail));
    s_trail_head = 0;
    s_touch_down = false;
    s_touch_x = 240;
    s_touch_y = 148;
    s_time = 0.0f;
    s_io1 = s_io2 = s_io40 = false;
    s_led_mode = 1;
    s_enc_pos = 0;

    sfx::init();
    hw::gpio_mode(1, hw::PIN_OUTPUT);
    hw::gpio_mode(2, hw::PIN_OUTPUT);
    hw::gpio_mode(40, hw::PIN_OUTPUT);
    sync_gpio_outputs();
    hw::neopixel_begin(48, 16);
    update_neopixels();

    Serial.println("== ESP32-4848S040C Hardware Diagnostics Cartridge ==");
    Serial.println("Commands: gpio <1|2|40> <0|1> | led <0|1|2> | tone | clear | status | help");
}

void app_step(float dt_sec)
{
    s_time += dt_sec;
    s_enc_pos += hw::encoder_consume_delta(0);
    for (int i = 0; i < TRAIL_MAX; ++i) {
        if (s_trail[i].age > 3) s_trail[i].age = (uint8_t)(s_trail[i].age - 3);
        else s_trail[i].age = 0;
    }
    update_neopixels();
}

void app_render(uint16_t* fb, int buf_idx)
{
    (void)buf_idx;
    const uint16_t cBg     = rgb565(12, 16, 26);
    const uint16_t cPanel  = rgb565(18, 26, 40);
    const uint16_t cBorder = rgb565(56, 92, 128);
    const uint16_t cAccent = rgb565(112, 226, 244);
    const uint16_t cGold   = rgb565(250, 205, 86);
    const uint16_t cGreen  = rgb565(78, 240, 154);
    const uint16_t cText   = rgb565(232, 242, 255);
    const uint16_t cMuted  = rgb565(132, 156, 186);

    fill_rect(fb, 0, 0, W, H, cBg);

    /* Top Header */
    fill_rect(fb, 0, 0, W, 46, rgb565(16, 24, 38));
    fill_rect(fb, 0, 44, W, 2, cAccent);
    draw_text(fb, 14, 15, "ESP32-S3 HW BENCH", cAccent, 2);

    bool relayMode = hw::jumper_state(hw::JUMPER_AUDIO_RELAY) != 0;
    uint16_t jCol = relayMode ? cGold : cGreen;
    fill_rect(fb, 316, 8, 152, 28, rgb565(24, 34, 52));
    stroke_rect(fb, 316, 8, 152, 28, jCol, 2);
    draw_text(fb, 326, 15, relayMode ? "R25-27 RLY" : "R21-23 I2S", jCol, 2);

    /* RGB565 Color Test Strip */
    for (int x = 0; x < PAD_W; ++x) {
        uint8_t v = (uint8_t)((x * 255) / PAD_W);
        uint16_t col = (x < PAD_W / 3)
            ? rgb565((uint8_t)((x * 255) / (PAD_W / 3)), 0, 0)
            : (x < (PAD_W * 2) / 3)
                ? rgb565(0, (uint8_t)(((x - PAD_W / 3) * 255) / (PAD_W / 3)), 0)
                : rgb565(0, 0, (uint8_t)(((x - (PAD_W * 2) / 3) * 255) / (PAD_W / 3)));
        (void)v;
        fill_rect(fb, PAD_X + x, PAD_Y, 1, 8, col);
    }

    /* Interactive Touchpad Area */
    fill_rect(fb, PAD_X, PAD_Y + 10, PAD_W, PAD_H - 10, cPanel);
    stroke_rect(fb, PAD_X, PAD_Y + 10, PAD_W, PAD_H - 10, cBorder, 2);

    /* Subtle calibration grid */
    uint16_t cGrid = rgb565(28, 40, 58);
    for (int gx = PAD_X + 56; gx < PAD_X + PAD_W; gx += 56) {
        fill_rect(fb, gx, PAD_Y + 12, 1, PAD_H - 14, cGrid);
    }
    for (int gy = PAD_Y + 48; gy < PAD_Y + PAD_H; gy += 46) {
        fill_rect(fb, PAD_X + 2, gy, PAD_W - 4, 1, cGrid);
    }

    /* Touch trail */
    for (int i = 0; i < TRAIL_MAX; ++i) {
        if (s_trail[i].age == 0) continue;
        uint8_t a = s_trail[i].age;
        uint16_t tc = rgb565((uint8_t)(a / 3), (uint8_t)((a * 226) / 255), a);
        int sz = (a > 160) ? 8 : 5;
        fill_rect(fb, s_trail[i].x - sz / 2, s_trail[i].y - sz / 2, sz, sz, tc);
    }

    /* Live Crosshair */
    if (s_touch_y >= PAD_Y + 12 && s_touch_y < PAD_Y + PAD_H - 2 &&
        s_touch_x >= PAD_X + 2 && s_touch_x < PAD_X + PAD_W - 2) {
        uint16_t chCol = s_touch_down ? cGold : cAccent;
        fill_rect(fb, s_touch_x - 14, s_touch_y, 29, 2, chCol);
        fill_rect(fb, s_touch_x, s_touch_y - 14, 2, 29, chCol);
    }

    char coord[40];
    snprintf(coord, sizeof(coord), "TOUCH X:%03d Y:%03d %s", s_touch_x, s_touch_y, s_touch_down ? "DN" : "UP");
    draw_text(fb, PAD_X + 12, PAD_Y + 20, coord, cText, 2);
    draw_text(fb, PAD_X + 12, PAD_Y + PAD_H - 24, "DRAG TO TEST GT911 CTP", cMuted, 2);

    /* 6 Tactile Control Buttons */
    const bool states[6] = { s_io1, s_io2, s_io40, s_led_mode > 0, false, false };
    for (int i = 0; i < 6; ++i) {
        const Btn& b = kBtns[i];
        bool on = states[i];
        uint16_t bg = on ? rgb565(24, 72, 64) : rgb565(22, 32, 48);
        uint16_t br = on ? cGreen : cBorder;
        fill_rect(fb, b.x, b.y, b.w, b.h, bg);
        stroke_rect(fb, b.x, b.y, b.w, b.h, br, 2);
        draw_text(fb, b.x + 12, b.y + 12, b.label, on ? cGreen : cText, 2);

        const char* sub = "";
        if (i < 3) sub = on ? "HIGH 3V3" : "LOW  0V";
        else if (i == 3) sub = (s_led_mode == 0) ? "OFF" : (s_led_mode == 1 ? "RAINBOW" : "CYAN");
        else if (i == 4) sub = relayMode ? "RLY MODE" : "PLAY";
        else sub = "RESET IO";
        draw_text(fb, b.x + 12, b.y + 36, sub, on ? cGold : cMuted, 2);
    }

    /* Footer Live Bus Status Strip */
    fill_rect(fb, 16, 426, 448, 40, rgb565(16, 22, 34));
    stroke_rect(fb, 16, 426, 448, 40, cBorder, 2);
    char bus[48];
    snprintf(bus, sizeof(bus), "BUS IO1:%d IO2:%d IO40:%d ENC:%ld",
             s_io1 ? 1 : 0, s_io2 ? 1 : 0, s_io40 ? 1 : 0, (long)s_enc_pos);
    draw_text(fb, 28, 439, bus, cAccent, 2);
}

void app_on_touch(int event_type, int x, int y)
{
    s_touch_x = x;
    s_touch_y = y;

    if (event_type == TOUCH_DOWN) {
        s_touch_down = true;
        if (x >= PAD_X && x < PAD_X + PAD_W && y >= PAD_Y + 10 && y < PAD_Y + PAD_H) {
            add_trail(x, y);
            return;
        }
        for (int i = 0; i < 6; ++i) {
            const Btn& b = kBtns[i];
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                press_button(i);
                return;
            }
        }
    } else if (event_type == TOUCH_MOVE) {
        if (s_touch_down && x >= PAD_X && x < PAD_X + PAD_W && y >= PAD_Y + 10 && y < PAD_Y + PAD_H) {
            add_trail(x, y);
        }
    } else if (event_type == TOUCH_UP) {
        s_touch_down = false;
    }
}

void app_on_uart(const char* line)
{
    if (!line) return;
    while (*line == ' ') line++;
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        Serial.println("[diag] gpio <1|2|40> <0|1> | led <0|1|2> | tone | clear | status");
        return;
    }
    if (strcmp(line, "status") == 0) {
        Serial.printf("[diag] IO1=%d IO2=%d IO40=%d LED=%d ENC=%ld JUMPER=%s\n",
                      s_io1 ? 1 : 0, s_io2 ? 1 : 0, s_io40 ? 1 : 0, s_led_mode, (long)s_enc_pos,
                      hw::jumper_state(hw::JUMPER_AUDIO_RELAY) ? "R25-27:RLY" : "R21-23:I2S");
        return;
    }
    if (strcmp(line, "tone") == 0) { press_button(4); return; }
    if (strcmp(line, "clear") == 0) { press_button(5); return; }
    if (strncmp(line, "led ", 4) == 0) {
        s_led_mode = atoi(line + 4) % 3;
        if (s_led_mode < 0) s_led_mode = 0;
        Serial.printf("[diag] LED mode = %d\n", s_led_mode);
        return;
    }
    if (strncmp(line, "gpio ", 5) == 0) {
        int pin = -1, val = 0;
        if (sscanf(line + 5, "%d %d", &pin, &val) == 2) {
            if (pin == 1) s_io1 = (val != 0);
            else if (pin == 2) s_io2 = (val != 0);
            else if (pin == 40) s_io40 = (val != 0);
            else hw::gpio_write(pin, val != 0);
            sync_gpio_outputs();
            Serial.printf("[diag] GPIO%d = %d\n", pin, val ? 1 : 0);
            return;
        }
    }
    Serial.printf("[diag] unknown cmd: %s (type 'help')\n", line);
}

void app_on_save() {}

void app_on_status_log(uint32_t fps)
{
    Serial.printf("[diag] fps=%u IO1=%d IO2=%d IO40=%d LED=%d\r\n",
                  (unsigned)fps, s_io1 ? 1 : 0, s_io2 ? 1 : 0, s_io40 ? 1 : 0, s_led_mode);
}

void app_audio_set_rate(uint32_t hz) { sfx::set_sample_rate(hz); }
void app_audio_render(float* out_f32, int frames) { sfx::render_f32(out_f32, frames); }
void app_audio_set_muted(bool muted) { sfx::set_muted(muted); }
bool app_audio_is_muted() { return sfx::is_muted(); }

} // namespace hw
