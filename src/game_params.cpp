/**
 * game_params.cpp — パラメータ実体とシリアル調整
 */
#include "game_params.h"
#include <Arduino.h>
#include <string.h>

namespace gp {

Params g = {
    /* .dmg_pct          */ 100,
    /* .move_pct         */ 100,
    /* .reach_pct        */ 100,
    /* .hp_pct           */ 100,
    /* .idle_x10         */ 5,     /* 0.5/s */
    /* .pet_buff_pct     */ 10,
    /* .pet_duration_min */ 10,
};

struct Entry { const char* key; uint32_t* value; };
static const Entry TABLE[] = {
    {"dmg_pct",   &g.dmg_pct},
    {"move_pct",  &g.move_pct},
    {"reach_pct", &g.reach_pct},
    {"hp_pct",    &g.hp_pct},
    {"idle_x10",  &g.idle_x10},
    {"pet_buff",  &g.pet_buff_pct},
    {"pet_min",   &g.pet_duration_min},
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
