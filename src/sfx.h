/**
 * sfx.h — リアルタイム 8 ボイス・チップチューン効果音シンセサイザ
 *
 * ESP32-S3 実機 (NS4168 I2S DAC: BCLK=GPIO1, LRCK=GPIO2, DOUT=GPIO40) と
 * Web シミュレータ (Emscripten WASM -> Web Audio API) の両方で
 * 完全に同一の C++ 合成エンジンを駆動する。
 */
#pragma once
#include <stdint.h>

namespace sfx {

enum Id : uint8_t {
    SFX_TAP = 0,
    SFX_CONFIRM,
    SFX_CANCEL,
    SFX_PAUSE,
    SFX_HIT_LIGHT,
    SFX_HIT_HEAVY,
    SFX_HIT_CRIT,
    SFX_PROJ_SHOOT,
    SFX_HURT_ALLY,
    SFX_ENEMY_KO,
    SFX_CRYSTAL,
    SFX_ULT_CAST,
    SFX_PET,
    SFX_BOX_OPEN,
    SFX_DRAFT_PICK,
    SFX_STEP,
    SFX_CAMP_REST,
    SFX_CHEST_OPEN,
    SFX_EVENT_GOOD,
    SFX_WAVE_CLEAR,
    SFX_BOSS_WARN,
    SFX_BOSS_WIN,
    SFX_GAME_OVER,
    SFX_ENDING,
    SFX_COUNT
};

void init();
void set_sample_rate(uint32_t hz);
uint32_t sample_rate();

void play(Id id);
void stop_all();

void set_muted(bool muted);
bool is_muted();
void set_volume(uint8_t pct);
uint8_t volume();

int active_voices();
const char* name(Id id);

/* ミキサ出力: 1フレーム = モノラル 1サンプル */
void render_i16(int16_t* dst, int frames);
void render_f32(float* dst, int frames);

} // namespace sfx
