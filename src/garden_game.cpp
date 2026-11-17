/**
 * garden_game.cpp — v3 経済コア実装
 */
#include "garden_game.h"
#include "game_params.h"
#include <string.h>
#include <math.h>
#include <Arduino.h>
#include <Preferences.h>

namespace game {

/* ツリー: col0(根) = 0,2,5,7 / col1 = 1,3,4,6,8 */
const SkillDef SKILLS[SKILL_N] = {
    /* 0 */ {"ATK+",   500.0,    -1},  /* ダメージ x1.25 */
    /* 1 */ {"ATK++",  20000.0,   0},  /* ダメージ x1.3 */
    /* 2 */ {"SPD+",   2000.0,   -1},  /* 移動 x1.3 */
    /* 3 */ {"SPD++",  50000.0,   2},  /* 移動 x1.3 */
    /* 4 */ {"RAGE+",   30000.0,   0},  /* 必殺ゲージ蓄積 x1.4 */
    /* 5 */ {"HP+",    5000.0,   -1},  /* 最大HP x2 */
    /* 6 */ {"IDLE+",  200000.0,  5},  /* 放置収入 x2 */
    /* 7 */ {"PET+",   3000.0,   -1},  /* なでなでバフ +15% */
    /* 8 */ {"PET++",  30000.0,   7},  /* バフ 30 分 */
};

static double s_idle_mult = 1.0;
static uint64_t s_buff_until_ms = 0;

State st = {};

/* ---------- セーブ (NVS, v2) ---------- */
struct SavePOD {
    uint32_t magic;          /* 'LUMI' */
    uint16_t ver;
    double crystals;
    double lifetime;
    uint8_t owned[SKILL_N];
    uint32_t pet_count;
    uint32_t wave_reached;
};

static Preferences prefs;

void save()
{
    SavePOD p{};
    p.magic = 0x4C554D49u;
    p.ver = 2;
    p.crystals = st.crystals;
    p.lifetime = st.lifetime;
    for (int i = 0; i < SKILL_N; i++) p.owned[i] = st.owned[i] ? 1 : 0;
    p.pet_count = st.pet_count;
    p.wave_reached = st.wave_reached;
    prefs.begin("garden", false);
    prefs.putBytes("save2", &p, sizeof(p));
    prefs.end();
}

/* スキル購入済みフラグから実効パラメータを再構築 */
static void apply_skill_effects()
{
    gp::g.dmg_pct   = 100;
    gp::g.move_pct  = 100;
    gp::g.rage_pct  = 100;
    gp::g.hp_pct    = 100;
    gp::g.idle_x10  = 5;
    gp::g.pet_buff_pct = 10;
    gp::g.pet_duration_min = 10;
    s_idle_mult = 1.0;

    if (st.owned[0]) gp::g.dmg_pct   = (uint32_t)(gp::g.dmg_pct   * 1.25);
    if (st.owned[1]) gp::g.dmg_pct   = (uint32_t)(gp::g.dmg_pct   * 1.30);
    if (st.owned[2]) gp::g.move_pct  = (uint32_t)(gp::g.move_pct  * 1.30);
    if (st.owned[3]) gp::g.move_pct  = (uint32_t)(gp::g.move_pct  * 1.30);
    if (st.owned[4]) gp::g.rage_pct  = (uint32_t)(gp::g.rage_pct  * 1.40);
    if (st.owned[5]) gp::g.hp_pct    = (uint32_t)(gp::g.hp_pct    * 2);
    if (st.owned[6]) { gp::g.idle_x10 *= 2; s_idle_mult *= 2.0; }
    if (st.owned[7]) gp::g.pet_buff_pct += 15;
    if (st.owned[8]) gp::g.pet_duration_min = 30;
}

void init()
{
    memset(&st, 0, sizeof(st));

    prefs.begin("garden", true);
    SavePOD p{};
    size_t got = prefs.getBytes("save2", &p, sizeof(p));
    prefs.end();

    if (got == sizeof(p) && p.magic == 0x4C554D49u && p.ver == 2) {
        st.crystals = p.crystals;
        st.lifetime = p.lifetime;
        for (int i = 0; i < SKILL_N; i++) st.owned[i] = p.owned[i] != 0;
        st.pet_count = p.pet_count;
        st.wave_reached = p.wave_reached;
        Serial.println("[game] save loaded");
    } else {
        st.crystals = 200.0;   /* 最初のスキルがすぐ買える (#017 最初の3分) */
        Serial.println("[game] new game");
    }
    apply_skill_effects();
}

/* ---------- 経済 ---------- */
double income_rate() { return gp::g.idle_x10 / 10.0 * s_idle_mult; }
double income_mult() { return s_idle_mult; }

double pet_mult()
{
    if (millis() < s_buff_until_ms) return 1.0 + gp::g.pet_buff_pct / 100.0;
    return 1.0;
}
bool pet_active() { return millis() < s_buff_until_ms; }
int  pet_remaining_s()
{
    if (!pet_active()) return 0;
    return (int)((s_buff_until_ms - millis()) / 1000u);
}

void pet_trigger()
{
    s_buff_until_ms = millis() + (uint64_t)gp::g.pet_duration_min * 60000u;
    st.pet_count++;
}

void tick(double dt)
{
    if (dt <= 0) return;
    double gain = income_rate() * dt * pet_mult();
    st.crystals += gain;
    st.lifetime += gain;
}

void add_drops(double v)
{
    double gain = v * pet_mult();
    st.crystals += gain;
    st.lifetime += gain;
}

void debug_give(double v)
{
    st.crystals += v;
    st.lifetime += (v > 0) ? v : 0;
}

/* ---------- スキル ---------- */
bool skill_locked(int i)
{
    if (i < 0 || i >= SKILL_N) return true;
    int8_t p = SKILLS[i].parent;
    return p >= 0 && !st.owned[p];
}
bool skill_affordable(int i)
{
    if (i < 0 || i >= SKILL_N) return false;
    return st.crystals >= SKILLS[i].cost;
}
bool buy_skill(int i)
{
    if (i < 0 || i >= SKILL_N) return false;
    if (st.owned[i] || skill_locked(i) || st.crystals < SKILLS[i].cost) return false;
    st.crystals -= SKILLS[i].cost;
    st.owned[i] = true;
    apply_skill_effects();
    save();
    return true;
}

/* ---------- 次の目標 (#023) ---------- */
/* 次の目標 (#023)。全て購入可能なら cheapest を READY で返す */
double next_goal(char* buf, size_t len)
{
    double best = 1e300;
    const char* name = "-";
    for (int i = 0; i < SKILL_N; i++) {
        if (st.owned[i] || skill_locked(i)) continue;
        double d = SKILLS[i].cost - st.crystals;
        if (d < best) { best = d; name = SKILLS[i].name; }
    }
    if (best < 0) best = 0;
    snprintf(buf, len, "%s", name);
    return best;
}

const char* fmt(double v, char* buf, size_t len)
{
    const char* sfx[] = {"", "K", "M", "B", "T", "P"};
    int s = 0;
    double x = v;
    while (x >= 1000.0 && s < 5) { x /= 1000.0; s++; }
    if (s == 0) snprintf(buf, len, "%d", (int)(x + 0.5));
    else snprintf(buf, len, "%.1f%s", x, sfx[s]);
    return buf;
}

} // namespace game
