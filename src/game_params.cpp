/**
 * game_params.cpp — パラメータの実体とシリアル調整
 */
#include "game_params.h"
#include <Arduino.h>
#include <string.h>

namespace gp {

Params g = {
    /* .seed_grow        */ 25,    /* 種: 2.5%/frame @30fps ≈ 0.75回/s */
    /* .crystal_grow     */ 6,     /* 結晶: ゆるやかに伝播 */
    /* .up_bias          */ 70,    /* 上方向成長 70% */
    /* .emit_water       */ 6,     /* 30fps で 5個/s の水 */
    /* .emit_sand        */ 5,
    /* .ambient_seed     */ 1200,  /* 40秒に1個 */
    /* .brush            */ 3,
};

struct Entry { const char* key; uint32_t* value; };
static const Entry TABLE[] = {
    {"seed_grow",    &g.seed_grow},
    {"crystal_grow", &g.crystal_grow},
    {"up_bias",      &g.up_bias},
    {"emit_water",   &g.emit_water},
    {"emit_sand",    &g.emit_sand},
    {"ambient_seed", &g.ambient_seed},
    {"brush",        &g.brush},
};

bool apply(const char* key, long value)
{
    for (const auto& e : TABLE) {
        if (strcmp(key, e.key) == 0) {
            *e.value = (value < 0) ? 0u : (uint32_t)value;
            Serial.printf("[param] %s = %u\r\n", key, (unsigned)*e.value);
            return true;
        }
    }
    return false;
}

void print_all()
{
    Serial.println("[param] --- current values ---");
    for (const auto& e : TABLE) {
        Serial.printf("[param] %-14s = %u\r\n", e.key, (unsigned)*e.value);
    }
}

} // namespace gp
