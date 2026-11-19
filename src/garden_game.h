/**
 * garden_game.h — 『ルミの水晶庭園』v4.0 ローグライト経済・成長・メタアンロックコア
 *
 * - 全50ウェーブ finite ローグライト (放置収入廃止: ドロップとウェーブ撃破のみ)
 * - 初期1人 (LUMI ★1) + ルートボックス (Charm / Scout New Talent 3択ドラフト)
 * - 最大3人出撃 / 同キャラ重複でランクアップ (*1 -> *2 -> ...)
 * - 周回アンロック (プール拡張のみ、初期能力引き継ぎなし) + NVS セーブ
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "roguelite_data.h"

namespace game {

struct PartySlot {
    bool active;
    uint8_t talent_id;
    uint8_t rank;
};

struct DraftCard {
    bool is_talent;
    uint8_t id;
    uint8_t rarity;
};

struct BuildMods {
    int dmg_pct;
    int hp_pct;
    int crit_pct;
    int spd_pct;
    int vamp_x10;
    int regen_x10;
    int chip_res_pct;
    int flat_armor;
    int drop_pct;
    int box_disc_pct;
    int rage_pct;
    int ult_pct;
    int wave_heal_pct;
    int boss_slay_pct;
    int exec_pct;
    int thorns;
    int shield_pct;
    int start_mp;
    int berserk_pct;
    int chain_spark;
    int frost_slow;
    int twin_bolt;
    int kill_heal_pct;
    int cleanse_ult;
    int scout_luck;
    int boss_guard_pct;
    int phoenix;
    int star_core;
};

struct State {
    /* 現ラン進行 */
    double crystals;
    uint16_t boxes_opened;
    uint8_t free_boxes;
    uint32_t wave_reached;
    PartySlot party[PARTY_MAX];
    uint8_t charm_lv[CHARM_N];
    bool draft_open;
    DraftCard draft[3];
    int8_t swap_talent;       /* >=0 のときは入替先スロット選択中 */
    bool phoenix_used;
    uint32_t pet_count;

    /* 永続メタ記録 (能力は引き継がない、プール解禁と図鑑のみ) */
    uint32_t run_number;
    uint32_t total_clears;
    uint32_t best_wave;
    uint32_t bosses_slain;
    uint8_t unlock_tier;
    uint64_t unlocked_talents;
    uint64_t discovered_talents;
    uint64_t unlocked_charms;
    uint8_t last_unlock_t;
    uint8_t last_unlock_c;

    /* 算出バフ */
    BuildMods mods;
};

extern State st;

void init();
void save();
void hard_reset();                          /* メタ含む完全初期化 (テスト用) */
void start_new_run();                       /* Runリセット (Wave1 / 初期1人 / Charm0) */
void on_run_end(uint32_t wave, bool cleared);
void on_boss_slain(uint32_t wave);
void tick(double dt);                       /* なでなでタイマーのみ (放置収入ゼロ) */
void recalc_mods();

void add_drops(double raw_crystals, int party_scavenger_bonus = 0);
void debug_give(double v);

double box_cost();
bool can_open_box();
bool open_lootbox(bool force_free = false);
int pick_draft(int card_idx, int swap_slot = -1);

/* デバッグ・検証ヘルパ */
bool debug_grant_talent(int tid, int slot_hint = -1);
bool debug_grant_charm(int cid);
void unlock_all_meta();

int party_count();
int total_charms_count();
int unlocked_talent_count();
int discovered_talent_count();
int unlocked_charm_count();

void pet_trigger();
bool pet_active();
int pet_remaining_s();
double pet_mult();

const char* fmt(double v, char* buf, size_t len);

} // namespace game
