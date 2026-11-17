/**
 * garden_game.h — 『ルミの水晶庭園』v3 経済コア
 * ベルトスクロール: 敵ドロップ + 放置収入 + スキルツリー(9) + なでなでバフ + NVS セーブ
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace game {

/* ---------- スキルツリー (9 ノード, 前提: parent) ---------- */
struct SkillDef {
    const char* name;
    double cost;
    int8_t parent;
};
constexpr int SKILL_N = 9;
extern const SkillDef SKILLS[SKILL_N];

/* ---------- 状態 ---------- */
struct State {
    double crystals;
    double lifetime;
    bool owned[SKILL_N];
    uint32_t pet_count;
    uint32_t wave_reached;
};
extern State st;

void init();
void save();
void tick(double dt);              /* 放置収入 (0.5/s 基礎 × IDLE+) */

double income_rate();              /* 放置収入 crystals/s */
double income_mult();              /* IDLE+ スキル倍率 */
double pet_mult();                 /* なでなでバフ中 1+x */

void add_drops(double v);          /* 敵ドロップ (バフ・倍率込みで加算) */
void debug_give(double v);

bool buy_skill(int i);
bool skill_locked(int i);
bool skill_affordable(int i);

void pet_trigger();                /* 縦スワイプ なでなで */
bool pet_active();
int  pet_remaining_s();

double next_goal(char* buf, size_t len);   /* 次のスキルまでの不足額 (#023) */
const char* fmt(double v, char* buf, size_t len);

} // namespace game
