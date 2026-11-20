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
    /* .rage_pct         */ 100,
    /* .hp_pct           */ 100,
    /* .crit_pct         */ 5,
    /* .air_pct          */ 100,
    /* .vamp_x10         */ 0,
    /* .ult_pct          */ 100,
    /* .drop_pct         */ 100,
    /* .enemy_hp_pct     */ 100,
    /* .enemy_dmg_pct    */ 100,
    /* .chip_pct         */ 100,
    /* .box_cost_pct     */ 100,
    /* .wave_heal_pct    */ 12,
    /* .pet_buff_pct     */ 10,
    /* .pet_duration_min */ 10,
    /* .hit_fx_frames    */ 10,
    /* .shake_pct        */ 100,
    /* .cutin_frames     */ 84,
    /* .victory_frames   */ 130,
};

struct Entry { const char* key; uint32_t* value; };
static const Entry TABLE[] = {
    {"dmg_pct",        &g.dmg_pct},
    {"move_pct",       &g.move_pct},
    {"rage_pct",       &g.rage_pct},
    {"hp_pct",         &g.hp_pct},
    {"crit_pct",       &g.crit_pct},
    {"air_pct",        &g.air_pct},
    {"vamp_x10",       &g.vamp_x10},
    {"ult_pct",        &g.ult_pct},
    {"drop_pct",       &g.drop_pct},
    {"enemy_hp_pct",   &g.enemy_hp_pct},
    {"enemy_dmg_pct",  &g.enemy_dmg_pct},
    {"chip_pct",       &g.chip_pct},
    {"box_cost_pct",   &g.box_cost_pct},
    {"wave_heal_pct",  &g.wave_heal_pct},
    {"pet_buff",       &g.pet_buff_pct},
    {"pet_min",        &g.pet_duration_min},
    {"hit_fx_frames",  &g.hit_fx_frames},
    {"shake_pct",      &g.shake_pct},
    {"cutin_frames",   &g.cutin_frames},
    {"victory_frames", &g.victory_frames},
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
