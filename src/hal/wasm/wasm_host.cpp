/**
 * wasm_host.cpp — Universal Emscripten WASM Host for ESP32-S3 Hardware Workbench
 *
 * Implements hw_hal.h and exports the universal Sandbox ABI v1 (_hw_*).
 * Any C++ firmware cartridge implementing hw_app.h compiles against this single
 * host with zero app-specific glue code.
 */
#ifdef __EMSCRIPTEN__

#include <Arduino.h>
#include <esp_timer.h>
#include <emscripten.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hw_hal.h"
#include "hal/hw_app.h"

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

/* ---------------- Virtual Bus State (Shared Memory with JS Workbench) ---------------- */
static uint8_t  g_gpio_dir[hw::MAX_GPIO];      // 0=in, 1=out, 2=in_pullup
static uint8_t  g_gpio_out[hw::MAX_GPIO];      // firmware output state (0/1)
static uint8_t  g_gpio_in[hw::MAX_GPIO];       // external 3D input state (0/1)
static float    g_pwm_duty[hw::MAX_GPIO];      // 0.0 .. 1.0
static uint16_t g_adc_val[hw::MAX_GPIO];       // 0 .. 4095
static int32_t  g_enc_delta[hw::MAX_ENCODERS]; // accumulated detent steps
static uint8_t  g_enc_btn[hw::MAX_ENCODERS];   // 0/1 pressed
static int32_t  g_jumpers[hw::MAX_JUMPERS];    // physical strapping jumpers

static uint8_t  g_led_rgb[hw::MAX_NEOPIXELS * 3];
static int      g_led_pin     = -1;
static int      g_led_count   = 0;
static uint32_t g_led_version = 0;

/* ---------------- Dynamic Double-Buffered RGB565 -> RGBA8888 Framebuffer ---------------- */
static int       g_fb_w = 0;
static int       g_fb_h = 0;
static uint16_t* g_canvas[2] = {nullptr, nullptr};
static uint32_t* g_rgba_buf  = nullptr;
static uint32_t  g_rgb565_lut[65536];
static bool      g_lut_ready = false;
static int       g_render_idx = 0;
static float     g_audio_f32[2048];

static void ensure_rgb565_lut()
{
    if (g_lut_ready) return;
    for (uint32_t c = 0; c < 65536; ++c) {
        uint32_t r = ((c >> 11) & 0x1Fu);
        uint32_t g = ((c >> 5)  & 0x3Fu);
        uint32_t b = ( c        & 0x1Fu);
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        g_rgb565_lut[c] = (0xFFu << 24) | (b << 16) | (g << 8) | r;
    }
    g_lut_ready = true;
}

static void convert_to_rgba(const uint16_t* src)
{
    const int n = g_fb_w * g_fb_h;
    uint32_t* dst = g_rgba_buf;
    for (int i = 0; i < n; ++i) {
        dst[i] = g_rgb565_lut[src[i]];
    }
}

/* ---------------- hw_hal.h Implementation ---------------- */
namespace hw {

uint32_t millis() { return ::millis(); }
int64_t  micros() { return esp_timer_get_time(); }

void log(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    web_serial_emit(buf);
}

int  display_width()   { return app_info().display_w; }
int  display_height()  { return app_info().display_h; }
bool display_is_mono() { return app_info().mono_display; }

void gpio_mode(int pin, PinMode mode)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    g_gpio_dir[pin] = (uint8_t)mode;
    if (mode == PIN_INPUT_PULLUP && g_gpio_in[pin] == 0) {
        // Default pull-up high unless explicitly pulled low externally
    }
}

void gpio_write(int pin, bool high)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    g_gpio_out[pin] = high ? 1 : 0;
    g_pwm_duty[pin] = high ? 1.0f : 0.0f;
}

bool gpio_read(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) return false;
    if (g_gpio_dir[pin] == PIN_OUTPUT) return g_gpio_out[pin] != 0;
    return g_gpio_in[pin] != 0;
}

void pwm_write(int pin, float duty)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    g_gpio_dir[pin] = PIN_OUTPUT;
    g_pwm_duty[pin] = duty;
    g_gpio_out[pin] = (duty >= 0.5f) ? 1 : 0;
}

uint16_t adc_read(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) return 0;
    return g_adc_val[pin];
}

int32_t encoder_consume_delta(int id)
{
    if (id < 0 || id >= MAX_ENCODERS) return 0;
    int32_t d = g_enc_delta[id];
    g_enc_delta[id] = 0;
    return d;
}

bool encoder_button(int id)
{
    if (id < 0 || id >= MAX_ENCODERS) return false;
    return g_enc_btn[id] != 0;
}

void neopixel_begin(int pin, int count)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    if (count < 0) count = 0;
    if (count > MAX_NEOPIXELS) count = MAX_NEOPIXELS;
    g_led_pin = pin;
    g_led_count = count;
    memset(g_led_rgb, 0, sizeof(g_led_rgb));
    g_led_version++;
}

void neopixel_set(int pin, int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (pin != g_led_pin || idx < 0 || idx >= g_led_count) return;
    g_led_rgb[idx * 3 + 0] = r;
    g_led_rgb[idx * 3 + 1] = g;
    g_led_rgb[idx * 3 + 2] = b;
}

void neopixel_clear(int pin)
{
    if (pin != g_led_pin) return;
    memset(g_led_rgb, 0, sizeof(g_led_rgb));
}

void neopixel_show(int pin)
{
    if (pin != g_led_pin) return;
    g_led_version++;
}

int jumper_state(int id)
{
    if (id < 0 || id >= MAX_JUMPERS) return 0;
    return g_jumpers[id];
}

} // namespace hw

/* ---------------- Universal Sandbox ABI v1 Exports ---------------- */
extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* hw_info_id() { return hw::app_info().id; }

EMSCRIPTEN_KEEPALIVE
const char* hw_info_name() { return hw::app_info().name; }

EMSCRIPTEN_KEEPALIVE
const char* hw_info_board() { return hw::app_info().default_board; }

EMSCRIPTEN_KEEPALIVE
int hw_fb_width() { return hw::app_info().display_w; }

EMSCRIPTEN_KEEPALIVE
int hw_fb_height() { return hw::app_info().display_h; }

EMSCRIPTEN_KEEPALIVE
int hw_fb_is_mono() { return hw::app_info().mono_display ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE
void hw_init()
{
    ensure_rgb565_lut();

    memset(g_gpio_dir, 0, sizeof(g_gpio_dir));
    memset(g_gpio_out, 0, sizeof(g_gpio_out));
    memset(g_gpio_in, 0, sizeof(g_gpio_in));
    memset(g_pwm_duty, 0, sizeof(g_pwm_duty));
    memset(g_adc_val, 0, sizeof(g_adc_val));
    memset(g_enc_delta, 0, sizeof(g_enc_delta));
    memset(g_enc_btn, 0, sizeof(g_enc_btn));
    memset(g_led_rgb, 0, sizeof(g_led_rgb));
    g_led_pin = -1;
    g_led_count = 0;
    g_led_version++;

    const auto& info = hw::app_info();
    g_fb_w = info.display_w;
    g_fb_h = info.display_h;
    const size_t px = (size_t)g_fb_w * (size_t)g_fb_h;

    if (g_canvas[0]) free(g_canvas[0]);
    if (g_canvas[1]) free(g_canvas[1]);
    if (g_rgba_buf)  free(g_rgba_buf);
    g_canvas[0] = (uint16_t*)calloc(px, sizeof(uint16_t));
    g_canvas[1] = (uint16_t*)calloc(px, sizeof(uint16_t));
    g_rgba_buf  = (uint32_t*)calloc(px, sizeof(uint32_t));
    g_render_idx = 0;

    hw::app_init();

    // Render initial boot frame into buffer 1 so buffer 0 starts cleanly next
    hw::app_render(g_canvas[1], 1);
    convert_to_rgba(g_canvas[1]);
    g_render_idx = 0;
}

EMSCRIPTEN_KEEPALIVE
void hw_step(float dt_sec)
{
    if (dt_sec <= 0.0f) dt_sec = 0.0166667f;
    if (dt_sec > 0.2f) dt_sec = 0.2f;
    hw::app_step(dt_sec);
}

EMSCRIPTEN_KEEPALIVE
void hw_render()
{
    if (!g_canvas[g_render_idx] || !g_rgba_buf) return;
    hw::app_render(g_canvas[g_render_idx], g_render_idx);
    convert_to_rgba(g_canvas[g_render_idx]);
    g_render_idx ^= 1;
}

EMSCRIPTEN_KEEPALIVE
uint8_t* hw_fb_rgba_ptr()
{
    return reinterpret_cast<uint8_t*>(g_rgba_buf);
}

EMSCRIPTEN_KEEPALIVE
void hw_touch_down(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= g_fb_w) x = g_fb_w - 1;
    if (y < 0) y = 0;
    if (y >= g_fb_h) y = g_fb_h - 1;
    hw::app_on_touch(hw::TOUCH_DOWN, x, y);
}

EMSCRIPTEN_KEEPALIVE
void hw_touch_move(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= g_fb_w) x = g_fb_w - 1;
    if (y < 0) y = 0;
    if (y >= g_fb_h) y = g_fb_h - 1;
    hw::app_on_touch(hw::TOUCH_MOVE, x, y);
}

EMSCRIPTEN_KEEPALIVE
void hw_touch_up(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= g_fb_w) x = g_fb_w - 1;
    if (y < 0) y = 0;
    if (y >= g_fb_h) y = g_fb_h - 1;
    hw::app_on_touch(hw::TOUCH_UP, x, y);
}

EMSCRIPTEN_KEEPALIVE
void hw_uart_rx(const char* line)
{
    if (!line) return;
    Serial.printf("> %s\n", line);
    hw::app_on_uart(line);
}

EMSCRIPTEN_KEEPALIVE
void hw_save()
{
    hw::app_on_save();
}

/* ---- Audio Stream ---- */
EMSCRIPTEN_KEEPALIVE
void hw_audio_set_rate(int hz)
{
    hw::app_audio_set_rate((uint32_t)(hz > 0 ? hz : 44100));
}

EMSCRIPTEN_KEEPALIVE
float* hw_audio_render(int frames)
{
    if (frames < 0) frames = 0;
    if (frames > 2048) frames = 2048;
    hw::app_audio_render(g_audio_f32, frames);
    return g_audio_f32;
}

EMSCRIPTEN_KEEPALIVE
void hw_audio_set_muted(int muted)
{
    hw::app_audio_set_muted(muted != 0);
}

EMSCRIPTEN_KEEPALIVE
int hw_audio_is_muted()
{
    return hw::app_audio_is_muted() ? 1 : 0;
}

/* ---- Virtual Bus Memory Pointers & Input Injectors ---- */
EMSCRIPTEN_KEEPALIVE
uint8_t* hw_gpio_out_ptr() { return g_gpio_out; }

EMSCRIPTEN_KEEPALIVE
uint8_t* hw_gpio_dir_ptr() { return g_gpio_dir; }

EMSCRIPTEN_KEEPALIVE
void hw_gpio_set_in(int pin, int high)
{
    if (pin < 0 || pin >= hw::MAX_GPIO) return;
    g_gpio_in[pin] = high ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
float* hw_pwm_ptr() { return g_pwm_duty; }

EMSCRIPTEN_KEEPALIVE
void hw_adc_set(int pin, int val)
{
    if (pin < 0 || pin >= hw::MAX_GPIO) return;
    if (val < 0) val = 0;
    if (val > 4095) val = 4095;
    g_adc_val[pin] = (uint16_t)val;
}

EMSCRIPTEN_KEEPALIVE
void hw_encoder_step(int id, int delta)
{
    if (id < 0 || id >= hw::MAX_ENCODERS) return;
    g_enc_delta[id] += delta;
}

EMSCRIPTEN_KEEPALIVE
void hw_encoder_btn(int id, int down)
{
    if (id < 0 || id >= hw::MAX_ENCODERS) return;
    g_enc_btn[id] = down ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t* hw_led_ptr() { return g_led_rgb; }

EMSCRIPTEN_KEEPALIVE
int hw_led_pin() { return g_led_pin; }

EMSCRIPTEN_KEEPALIVE
int hw_led_count() { return g_led_count; }

EMSCRIPTEN_KEEPALIVE
uint32_t hw_led_version() { return g_led_version; }

EMSCRIPTEN_KEEPALIVE
void hw_jumper_set(int id, int state)
{
    if (id < 0 || id >= hw::MAX_JUMPERS) return;
    g_jumpers[id] = state;
}

} // extern "C"

#endif // __EMSCRIPTEN__
