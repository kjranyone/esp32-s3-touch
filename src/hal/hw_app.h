#pragma once
/**
 * hw_app.h — Universal ESP32-S3 Firmware Cartridge Entry Interface
 *
 * Every cartridge under src/apps/<id>/ implements these functions.
 * Both the real ESP32-S3 host (src/main.cpp) and the WASM host (src/hal/wasm/wasm_host.cpp)
 * drive any cartridge solely through this ABI.
 */
#include <stdint.h>

namespace hw {

struct AppInfo {
    const char* id;            // Stable machine ID, e.g. "crystal_garden"
    const char* name;          // Display title, e.g. "Crystal Garden"
    const char* default_board; // Default board manifest ID, e.g. "esp32_4848s040c"
    int         display_w;     // Framebuffer width in pixels (e.g. 480 or 128)
    int         display_h;     // Framebuffer height in pixels (e.g. 480 or 64)
    bool        mono_display;  // false = RGB565, true = monochrome OLED (lit if pixel != 0)
};

// Required cartridge lifecycle callbacks
const AppInfo& app_info();
void app_init();
void app_step(float dt_sec);
// buf_idx is 0 or 1 for double-buffered dirty-rect / SRAM band renderers
void app_render(uint16_t* fb_rgb565, int buf_idx);

// Input & system events
void app_on_touch(int event_type, int x, int y); // hw::TOUCH_DOWN / MOVE / UP
void app_on_uart(const char* line);
void app_on_save();

// Optional periodic status log hook (called ~every 10s on real hardware)
void app_on_status_log(uint32_t fps);

// Audio stream hooks (called by Web Audio / I2S host)
void app_audio_set_rate(uint32_t hz);
void app_audio_render(float* out_f32, int frames);
void app_audio_set_muted(bool muted);
bool app_audio_is_muted();

} // namespace hw
