/**
 * app_oled_pocket.cpp — ESP32-S3-DevKitC + 0.96" 128x64 OLED Pocket Synth & Oscilloscope
 *
 * Real ESP32-S3 C++ firmware targeting hal/hw_hal.h + hal/hw_app.h.
 * Renders a dual-zone Yellow/Cyan 128x64 SSD1306 display with a live audio
 * oscilloscope, 8-step arpeggiator, resonant filter swept by the 3D EC11
 * rotary encoder, and beat-synced WS2812B 16-LED ring + onboard GPIO48 LED.
 */
#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hw_hal.h"
#include "hal/hw_app.h"
#include "font5x7.inc"

namespace {

static constexpr int W = 128;
static constexpr int H = 64;
static constexpr int LED_COUNT = 16;
static constexpr float TAU = 6.28318530718f;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Authentic dual-color SSD1306 phosphor palette: top 16px = amber-yellow, bottom 48px = ice-cyan
static inline uint16_t oled_pixel_color(int y, bool dim = false)
{
    if (y < 16) {
        return dim ? rgb565(120, 92, 18) : rgb565(255, 214, 72);
    }
    return dim ? rgb565(22, 92, 118) : rgb565(74, 234, 255);
}

static void put_px(uint16_t* fb, int x, int y, bool dim = false)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    fb[y * W + x] = oled_pixel_color(y, dim);
}

static void fill_rect_oled(uint16_t* fb, int x, int y, int w, int h, bool on, bool dim = false)
{
    const uint16_t bg = rgb565(3, 5, 8);
    for (int r = 0; r < h; ++r) {
        int py = y + r;
        if (py < 0 || py >= H) continue;
        uint16_t col = on ? oled_pixel_color(py, dim) : bg;
        for (int c = 0; c < w; ++c) {
            int px = x + c;
            if (px >= 0 && px < W) fb[py * W + px] = col;
        }
    }
}

static void draw_str(uint16_t* fb, int x, int y, const char* s, bool dim = false)
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
                    if (bits & (0x10 >> c)) put_px(fb, cx + c, y + r, dim);
                }
            }
        }
        cx += 6;
    }
}

/* ---------------- Real-Time DSP Pocket Synth ---------------- */
static uint32_t s_sr         = 44100;
static bool     s_muted      = false;
static bool     s_playing    = true;
static int      s_wave_type  = 0;     // 0=SAW, 1=PLS, 2=FM, 3=SUB
static int      s_bpm        = 124;
static float    s_cutoff_hz  = 1850.0f;
static float    s_reso       = 0.52f;
static int      s_step_idx   = 0;
static float    s_step_accum = 0.0f;

// D minor pentatonic / lush synthwave arpeggio (MIDI notes)
static const uint8_t kArpNotes[8] = { 50, 57, 62, 65, 69, 65, 62, 60 };
static const char*   kWaveNames[4] = { "SAW", "PLS", "FM ", "SUB" };

static float s_phase   = 0.0f;
static float s_mod_ph  = 0.0f;
static float s_freq    = 146.83f;
static float s_env     = 0.0f;
static float s_svf_lp  = 0.0f;
static float s_svf_bp  = 0.0f;
static float s_vis_env = 0.0f;

// Live oscilloscope ring buffer filled from actual synthesized DSP audio
static float s_scope[256];
static int   s_scope_wr = 0;
static int   s_scope_decim = 0;

static bool  s_enc_btn_prev = false;
static bool  s_ext_btn_prev = false;
static float s_time = 0.0f;

static float midi_to_hz(uint8_t note)
{
    return 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
}

static void trigger_note(uint8_t midi)
{
    s_freq = midi_to_hz(midi);
    s_env = 1.0f;
    s_vis_env = 1.0f;
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

static void update_leds()
{
    float baseHue = (float)s_wave_type * 85.0f + (s_cutoff_hz / 7200.0f) * 90.0f;
    for (int i = 0; i < LED_COUNT; ++i) {
        int stepSlot = (i / 2) % 8;
        bool isCur = (stepSlot == s_step_idx);
        float bright = isCur ? (0.35f + 0.65f * s_vis_env) : 0.08f;
        uint8_t r, g, b;
        hsv_to_rgb(baseHue + i * 14.0f, 0.92f, bright, r, g, b);
        hw::neopixel_set(48, i, r, g, b);
    }
    hw::neopixel_show(48);
}

static void render_dsp_sample_step(float& out)
{
    float inc = s_freq / (float)s_sr;
    s_phase += inc;
    if (s_phase >= 1.0f) s_phase -= 1.0f;

    float osc = 0.0f;
    if (s_wave_type == 0) {
        osc = 2.0f * s_phase - 1.0f; // Sawtooth
    } else if (s_wave_type == 1) {
        float pw = 0.25f + 0.25f * sinf(s_time * 1.8f);
        osc = (s_phase < pw) ? 0.85f : -0.85f; // PWM Pulse
    } else if (s_wave_type == 2) {
        s_mod_ph += inc * 2.0f;
        if (s_mod_ph >= 1.0f) s_mod_ph -= 1.0f;
        float idx = 2.4f * s_env;
        osc = sinf(TAU * s_phase + idx * sinf(TAU * s_mod_ph)); // 2-op FM Bell
    } else {
        osc = 0.72f * sinf(TAU * s_phase) + 0.28f * ((s_phase < 0.5f) ? 1.0f : -1.0f); // Sub Bass
    }

    // Chamberlin State-Variable Resonant Low-Pass Filter
    float f = 2.0f * sinf(3.14159265f * fminf(s_cutoff_hz, (float)s_sr * 0.38f) / (float)s_sr);
    float q = 1.0f - s_reso * 0.78f;
    float hp = osc - s_svf_lp - q * s_svf_bp;
    s_svf_bp += f * hp;
    s_svf_lp += f * s_svf_bp;
    // Clamp SVF state for stability
    if (s_svf_lp > 4.0f) s_svf_lp = 4.0f;
    if (s_svf_lp < -4.0f) s_svf_lp = -4.0f;
    if (s_svf_bp > 4.0f) s_svf_bp = 4.0f;
    if (s_svf_bp < -4.0f) s_svf_bp = -4.0f;

    // Exponential decay envelope
    float decay = expf(-1.0f / (0.22f * (float)s_sr));
    s_env *= decay;

    float sig = tanhf(s_svf_lp * 1.15f) * s_env * 0.48f;
    out = sig;

    // Decimate into 256-sample scope buffer for crisp OLED waveform rendering
    if (++s_scope_decim >= 2) {
        s_scope_decim = 0;
        s_scope[s_scope_wr] = sig * 2.1f;
        s_scope_wr = (s_scope_wr + 1) & 255;
    }
}

} // anonymous namespace

namespace hw {

const AppInfo& app_info()
{
    static const AppInfo kInfo = {
        "oled_pocket",
        "OLED Pocket Synth",
        "esp32s3_devkitc",
        W,
        H,
        false,
    };
    return kInfo;
}

void app_init()
{
    s_playing = true;
    s_wave_type = 0;
    s_bpm = 124;
    s_cutoff_hz = 1850.0f;
    s_step_idx = 0;
    s_step_accum = 0.0f;
    s_phase = s_mod_ph = s_svf_lp = s_svf_bp = 0.0f;
    s_time = 0.0f;
    s_enc_btn_prev = false;
    s_ext_btn_prev = false;
    memset(s_scope, 0, sizeof(s_scope));

    hw::gpio_mode(47, hw::PIN_INPUT);
    hw::neopixel_begin(48, LED_COUNT);
    trigger_note(kArpNotes[0]);
    update_leds();

    // Pre-seed scope buffer so initial boot frame shows a crisp waveform immediately
    for (int i = 0; i < 512; ++i) {
        float tmp = 0;
        render_dsp_sample_step(tmp);
    }

    Serial.println("== ESP32-S3-DevKitC + 0.96\" OLED Pocket Synth ==");
    Serial.println("Controls: Turn EC11 = Filter Cutoff | Click EC11 = Waveform | Red BTN = Play/Pause");
    Serial.println("Commands: bpm <60..200> | wave <0..3> | play <0|1> | note <midi> | status | help");
}

void app_step(float dt_sec)
{
    s_time += dt_sec;
    s_vis_env *= expf(-dt_sec * 5.2f);

    // 1. EC11 Rotary Encoder -> sweeps Filter Cutoff smoothly (logarithmic feel)
    int32_t d = hw::encoder_consume_delta(0);
    if (d != 0) {
        s_cutoff_hz *= powf(1.12f, (float)d);
        if (s_cutoff_hz < 220.0f) s_cutoff_hz = 220.0f;
        if (s_cutoff_hz > 7200.0f) s_cutoff_hz = 7200.0f;
        Serial.printf("[synth] Cutoff -> %.0f Hz\n", s_cutoff_hz);
    }

    // 2. EC11 Shaft Click -> cycles Synth Waveform
    bool encBtn = hw::encoder_button(0);
    if (encBtn && !s_enc_btn_prev) {
        s_wave_type = (s_wave_type + 1) % 4;
        trigger_note(kArpNotes[s_step_idx]);
        Serial.printf("[synth] Waveform -> %s\n", kWaveNames[s_wave_type]);
    }
    s_enc_btn_prev = encBtn;

    // 3. Companion Red Tactile Button (GPIO 47) -> Play/Pause Sequencer
    bool extBtn = hw::gpio_read(47);
    if (extBtn && !s_ext_btn_prev) {
        s_playing = !s_playing;
        if (s_playing) trigger_note(kArpNotes[s_step_idx]);
        Serial.printf("[synth] Sequencer -> %s\n", s_playing ? "PLAY" : "PAUSE");
    }
    s_ext_btn_prev = extBtn;

    // 4. 8-Step 16th-Note Arpeggiator Clock
    if (s_playing) {
        float stepDur = 60.0f / ((float)s_bpm * 2.0f); // eighth-note pulse
        s_step_accum += dt_sec;
        if (s_step_accum >= stepDur) {
            s_step_accum -= stepDur;
            s_step_idx = (s_step_idx + 1) & 7;
            trigger_note(kArpNotes[s_step_idx]);
        }
    }

    // Keep oscilloscope alive even before browser Web Audio is unlocked
    if (s_muted || s_vis_env > 0.05f) {
        for (int i = 0; i < 64; ++i) {
            float dummy = 0;
            render_dsp_sample_step(dummy);
        }
    }

    update_leds();
}

void app_render(uint16_t* fb, int buf_idx)
{
    (void)buf_idx;
    const uint16_t cBg = rgb565(3, 5, 8);
    for (int i = 0; i < W * H; ++i) fb[i] = cBg;

    /* ---- Top Yellow Zone (y = 0..15): Header Telemetry ---- */
    char top[24];
    snprintf(top, sizeof(top), "%s %3dBPM %s", kWaveNames[s_wave_type], s_bpm, s_playing ? "RUN" : "HLD");
    draw_str(fb, 2, 4, top, false);
    // Crisp divider rule at y = 14
    for (int x = 0; x < W; ++x) put_px(fb, x, 14, true);

    /* ---- Bottom Cyan Zone (y = 16..63): Live Oscilloscope + Cutoff + 8 Steps ---- */
    // Subtle center zero-axis dotted line
    for (int x = 2; x < W - 2; x += 4) put_px(fb, x, 33, true);

    // Find rising zero-crossing in scope buffer for rock-solid trigger lock
    int trig = (s_scope_wr - 140) & 255;
    for (int i = 0; i < 120; ++i) {
        int idx0 = (trig + i) & 255;
        int idx1 = (trig + i + 1) & 255;
        if (s_scope[idx0] <= 0.0f && s_scope[idx1] > 0.0f) {
            trig = idx1;
            break;
        }
    }

    int prevY = 33;
    for (int x = 0; x < W; ++x) {
        float v = s_scope[(trig + x) & 255];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int py = 33 - (int)(v * 14.0f);
        if (py < 17) py = 17;
        if (py > 48) py = 48;

        int y0 = (x == 0) ? py : prevY;
        int yLo = (y0 < py) ? y0 : py;
        int yHi = (y0 > py) ? y0 : py;
        for (int y = yLo; y <= yHi; ++y) put_px(fb, x, y, false);
        prevY = py;
    }

    /* Filter Cutoff Bar (y = 51..54) */
    float cutFrac = (logf(s_cutoff_hz / 220.0f) / logf(7200.0f / 220.0f));
    if (cutFrac < 0.0f) cutFrac = 0.0f;
    if (cutFrac > 1.0f) cutFrac = 1.0f;
    int barW = (int)(cutFrac * 124.0f);
    fill_rect_oled(fb, 2, 51, 124, 3, true, true);
    fill_rect_oled(fb, 2, 51, barW, 3, true, false);

    /* 8-Step Arpeggiator Blocks (y = 57..62) */
    for (int i = 0; i < 8; ++i) {
        int bx = 2 + i * 16;
        bool cur = (i == s_step_idx);
        fill_rect_oled(fb, bx, 57, 13, 5, true, !cur);
    }
}

void app_on_touch(int event_type, int x, int y)
{
    (void)x; (void)y;
    if (event_type != TOUCH_DOWN) return;
    // Clicking the 0.96" OLED cycles waveform & triggers a crisp note
    s_wave_type = (s_wave_type + 1) % 4;
    trigger_note(kArpNotes[s_step_idx]);
    Serial.printf("[synth] OLED click -> Waveform %s\n", kWaveNames[s_wave_type]);
}

void app_on_uart(const char* line)
{
    if (!line) return;
    while (*line == ' ') line++;
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        Serial.println("[synth] bpm <60..200> | wave <0..3> | play <0|1> | note <midi> | status");
        return;
    }
    if (strcmp(line, "status") == 0) {
        Serial.printf("[synth] wave=%s bpm=%d cutoff=%.0fHz play=%d step=%d\n",
                      kWaveNames[s_wave_type], s_bpm, s_cutoff_hz, s_playing ? 1 : 0, s_step_idx);
        return;
    }
    if (strncmp(line, "bpm ", 4) == 0) {
        int b = atoi(line + 4);
        if (b < 60) b = 60;
        if (b > 200) b = 200;
        s_bpm = b;
        Serial.printf("[synth] BPM -> %d\n", s_bpm);
        return;
    }
    if (strncmp(line, "wave ", 5) == 0) {
        int w = atoi(line + 5);
        if (w >= 0 && w < 4) {
            s_wave_type = w;
            Serial.printf("[synth] Waveform -> %s\n", kWaveNames[s_wave_type]);
            return;
        }
    }
    if (strncmp(line, "play ", 5) == 0) {
        s_playing = atoi(line + 5) != 0;
        Serial.printf("[synth] Play -> %d\n", s_playing ? 1 : 0);
        return;
    }
    if (strncmp(line, "note ", 5) == 0) {
        int n = atoi(line + 5);
        if (n >= 24 && n <= 96) {
            trigger_note((uint8_t)n);
            Serial.printf("[synth] Trigger MIDI %d\n", n);
            return;
        }
    }
    Serial.printf("[synth] unknown cmd: %s\n", line);
}

void app_on_save() {}

void app_on_status_log(uint32_t fps)
{
    Serial.printf("[synth] fps=%u wave=%s bpm=%d cutoff=%.0fHz\r\n",
                  (unsigned)fps, kWaveNames[s_wave_type], s_bpm, s_cutoff_hz);
}

void app_audio_set_rate(uint32_t hz) { s_sr = (hz >= 8000 && hz <= 96000) ? hz : 44100; }
void app_audio_set_muted(bool muted) { s_muted = muted; }
bool app_audio_is_muted() { return s_muted; }

void app_audio_render(float* out_f32, int frames)
{
    if (!out_f32 || frames <= 0) return;
    if (s_muted) {
        memset(out_f32, 0, sizeof(float) * (size_t)frames);
        return;
    }
    for (int i = 0; i < frames; ++i) {
        render_dsp_sample_step(out_f32[i]);
    }
}

} // namespace hw
