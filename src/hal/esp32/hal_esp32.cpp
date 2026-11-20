/**
 * hal_esp32.cpp — Real ESP32-S3 Hardware Backend for hw_hal.h
 *
 * Uses Arduino-ESP32 3.1.x APIs (GPIO, LEDC PWM, ADC, esp_timer, Serial).
 */
#ifndef __EMSCRIPTEN__

#include "hal/hw_hal.h"
#include "hal/hw_app.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace hw {

static uint8_t  s_neo_buf[MAX_NEOPIXELS * 3];
static int      s_neo_pin   = -1;
static int      s_neo_count = 0;
static int      s_jumpers[MAX_JUMPERS] = {0};

uint32_t millis() { return ::millis(); }
int64_t  micros() { return esp_timer_get_time(); }

void log(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
}

int  display_width()   { return app_info().display_w; }
int  display_height()  { return app_info().display_h; }
bool display_is_mono() { return app_info().mono_display; }

void gpio_mode(int pin, PinMode mode)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    if (mode == PIN_OUTPUT) pinMode(pin, OUTPUT);
    else if (mode == PIN_INPUT_PULLUP) pinMode(pin, INPUT_PULLUP);
    else pinMode(pin, INPUT);
}

void gpio_write(int pin, bool high)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    digitalWrite(pin, high ? HIGH : LOW);
}

bool gpio_read(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) return false;
    return digitalRead(pin) != LOW;
}

void pwm_write(int pin, float duty)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    analogWrite(pin, (int)(duty * 255.0f + 0.5f));
}

uint16_t adc_read(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) return 0;
    int v = analogRead(pin);
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)v;
}

int32_t encoder_consume_delta(int id)
{
    (void)id;
    return 0;
}

bool encoder_button(int id)
{
    (void)id;
    return false;
}

void neopixel_begin(int pin, int count)
{
    if (pin < 0 || pin >= MAX_GPIO) return;
    if (count < 0) count = 0;
    if (count > MAX_NEOPIXELS) count = MAX_NEOPIXELS;
    s_neo_pin = pin;
    s_neo_count = count;
    memset(s_neo_buf, 0, sizeof(s_neo_buf));
}

void neopixel_set(int pin, int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (pin != s_neo_pin || idx < 0 || idx >= s_neo_count) return;
    s_neo_buf[idx * 3 + 0] = r;
    s_neo_buf[idx * 3 + 1] = g;
    s_neo_buf[idx * 3 + 2] = b;
}

void neopixel_clear(int pin)
{
    if (pin != s_neo_pin) return;
    memset(s_neo_buf, 0, sizeof(s_neo_buf));
}

void neopixel_show(int pin)
{
    if (pin != s_neo_pin || s_neo_count <= 0) return;
    // Built-in single-LED shortcut via Arduino-ESP32 neopixelWrite for onboard RGB LED
    if (s_neo_count == 1) {
        rgbLedWrite(pin, s_neo_buf[0], s_neo_buf[1], s_neo_buf[2]);
    }
}

int jumper_state(int id)
{
    if (id < 0 || id >= MAX_JUMPERS) return 0;
    return s_jumpers[id];
}

} // namespace hw

#endif // !__EMSCRIPTEN__
