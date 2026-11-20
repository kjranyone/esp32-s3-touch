#pragma once
/**
 * hw_hal.h — Portable ESP32-S3 Hardware Abstraction Layer
 *
 * Implemented identically by:
 *   - src/hal/esp32/hal_esp32.cpp  (real ESP32-S3 silicon via PlatformIO / Arduino-ESP32)
 *   - src/hal/wasm/wasm_host.cpp   (Web 3D Hardware Workbench via Emscripten WASM)
 *
 * Firmware cartridges under src/apps/<name>/ target this header so the exact same
 * C++ source builds cleanly for both real hardware and the 3D web simulator.
 */
#include <stdint.h>
#include <stddef.h>

namespace hw {

static constexpr int MAX_GPIO      = 49;  // ESP32-S3 GPIO 0..48
static constexpr int MAX_ENCODERS  = 4;
static constexpr int MAX_NEOPIXELS = 64;

enum PinMode : uint8_t {
    PIN_INPUT        = 0,
    PIN_OUTPUT       = 1,
    PIN_INPUT_PULLUP = 2,
};

enum TouchEvent : int {
    TOUCH_DOWN = 0,
    TOUCH_MOVE = 1,
    TOUCH_UP   = 2,
};

// Board-level physical strapping / solder-bridge jumper IDs
enum JumperId : int {
    // ESP32-4848S040C: 0 = R21-23 populated (NS4168 I2S Audio), 1 = R25-27 populated (3ch Relay)
    JUMPER_AUDIO_RELAY = 0,
    MAX_JUMPERS        = 8,
};

// ---- Timing & Diagnostics ----
uint32_t millis();
int64_t  micros();
void     log(const char* fmt, ...);

// ---- Display Geometry ----
int  display_width();
int  display_height();
bool display_is_mono();

// ---- Digital GPIO (GPIO 0..48) ----
void gpio_mode(int pin, PinMode mode);
void gpio_write(int pin, bool high);
bool gpio_read(int pin);

// ---- PWM (0.0f .. 1.0f duty) ----
void pwm_write(int pin, float duty);

// ---- ADC (12-bit: 0 .. 4095) ----
uint16_t adc_read(int pin);

// ---- Rotary Encoders (detent delta + push switch) ----
int32_t encoder_consume_delta(int id);
bool    encoder_button(int id);

// ---- WS2812B / NeoPixel (RMT) ----
void neopixel_begin(int pin, int count);
void neopixel_set(int pin, int idx, uint8_t r, uint8_t g, uint8_t b);
void neopixel_clear(int pin);
void neopixel_show(int pin);

// ---- Physical Jumper / Hardware Strapping ----
int jumper_state(int id);

} // namespace hw
