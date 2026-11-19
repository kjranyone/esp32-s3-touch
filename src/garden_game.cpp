/**
 * garden_game.cpp — 『ルミの水晶庭園』v4.0 ローグライト経済・ルートボックス・メタ進行実装
 */
#include "garden_game.h"
#include "game_params.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>

namespace game {

State st = {};
static uint64_t s_buff_until_ms = 0;
static Preferences prefs;

struct SavePOD {
    uint32_t magic;            /* 'LUMI' */
    uint16_t ver;              /* 4 */
    double crystals;
    uint16_t boxes_opened;
    uint8_t free_boxes;
    uint32_t wave_reached;
    PartySlot party[PARTY_MAX];
    uint8_t charm_lv[CHARM_N];
    uint8_t draft_open;
    DraftCard draft[3];
    uint8_t phoenix_used;
    uint32_t pet_count;
    uint32_t run_number;
    uint32_t total_clears;
    uint32_t best_wave;
    uint32_t bosses_slain;
    uint8_t unlock_tier;
    uint64_t unlocked_talents;
    uint64_t discovered_talents;
    uint64_t unlocked_charms;
};

static uint32_t rl_rand()
{
    uint32_t r = esp_random();
    return r ? r : (uint32_t)(esp_timer_get_time() ^ 0x9E3779B9u);
}

static float rl_frand01()
{
    return (float)(rl_rand() & 0xFFFFFFu) / 16777216.0f;
}

static void apply_unlock_tier(uint8_t new_tier, uint8_t* out_t = nullptr, uint8_t* out_c = nullptr)
{
    if (new_tier > st.unlock_tier) st.unlock_tier = new_tier;
    uint8_t nt = 0, nc = 0;
    for (int i = 0; i < TALENT_N; ++i) {
        if (TALENTS[i].unlock_tier <= st.unlock_tier) {
            uint64_t bit = 1ULL << i;
            if (!(st.unlocked_talents & bit)) {
                st.unlocked_talents |= bit;
                nt++;
            }
        }
    }
    for (int i = 0; i < CHARM_N; ++i) {
        if (CHARMS[i].unlock_tier <= st.unlock_tier) {
            uint64_t bit = 1ULL << i;
            if (!(st.unlocked_charms & bit)) {
                st.unlocked_charms |= bit;
                nc++;
            }
        }
    }
    if (out_t) *out_t = nt;
    if (out_c) *out_c = nc;
}

void save()
{
    SavePOD p{};
    p.magic = 0x4C554D49u;
    p.ver = 4;
    p.crystals = st.crystals;
    p.boxes_opened = st.boxes_opened;
    p.free_boxes = st.free_boxes;
    p.wave_reached = st.wave_reached;
    memcpy(p.party, st.party, sizeof(p.party));
    memcpy(p.charm_lv, st.charm_lv, sizeof(p.charm_lv));
    p.draft_open = st.draft_open ? 1 : 0;
    memcpy(p.draft, st.draft, sizeof(p.draft));
    p.phoenix_used = st.phoenix_used ? 1 : 0;
    p.pet_count = st.pet_count;
    p.run_number = st.run_number;
    p.total_clears = st.total_clears;
    p.best_wave = st.best_wave;
    p.bosses_slain = st.bosses_slain;
    p.unlock_tier = st.unlock_tier;
    p.unlocked_talents = st.unlocked_talents;
    p.discovered_talents = st.discovered_talents;
    p.unlocked_charms = st.unlocked_charms;

    prefs.begin("garden", false);
    prefs.putBytes("save4", &p, sizeof(p));
    prefs.end();
}

void recalc_mods()
{
    BuildMods& m = st.mods;
    memset(&m, 0, sizeof(m));
    m.dmg_pct = 100;
    m.hp_pct = 100;
    m.crit_pct = 5;
    m.spd_pct = 100;
    m.drop_pct = 100;
    m.rage_pct = 100;
    m.ult_pct = 100;

    for (int i = 0; i < CHARM_N; ++i) {
        int lv = st.charm_lv[i];
        if (lv <= 0) continue;
        int v = (int)CHARMS[i].val * lv;
        switch (CHARMS[i].eff) {
        case CE_ATK_PCT:        m.dmg_pct += v; break;
        case CE_HP_PCT:         m.hp_pct += v; break;
        case CE_CRIT_PCT:       m.crit_pct += v; break;
        case CE_SPD_PCT:        m.spd_pct += v; break;
        case CE_VAMP_X10:       m.vamp_x10 += v; break;
        case CE_REGEN_X10:      m.regen_x10 += v; break;
        case CE_CHIP_RES_PCT:   m.chip_res_pct += v; break;
        case CE_FLAT_ARMOR:     m.flat_armor += v; break;
        case CE_DROP_PCT:       m.drop_pct += v; break;
        case CE_BOX_DISC_PCT:   m.box_disc_pct += v; break;
        case CE_RAGE_PCT:       m.rage_pct += v; break;
        case CE_ULT_PCT:        m.ult_pct += v; break;
        case CE_WAVE_HEAL_PCT:  m.wave_heal_pct += v; break;
        case CE_BOSS_SLAY_PCT:  m.boss_slay_pct += v; break;
        case CE_EXEC_PCT:       m.exec_pct += v; break;
        case CE_THORNS:         m.thorns += v; break;
        case CE_SHIELD_PCT:     m.shield_pct += v; break;
        case CE_START_MP:       m.start_mp += v; break;
        case CE_GLASS_ATK:      m.dmg_pct += v; m.hp_pct -= 16 * lv; break;
        case CE_BERSERK_PCT:    m.berserk_pct += v; break;
        case CE_CHAIN_SPARK:    m.chain_spark += v; break;
        case CE_FROST_SLOW:     m.frost_slow += v; break;
        case CE_TWIN_BOLT:      m.twin_bolt += v; break;
        case CE_KILL_HEAL_PCT:  m.kill_heal_pct += v; break;
        case CE_CLEANSE_ULT:    m.cleanse_ult += lv; break;
        case CE_SCOUT_LUCK:     m.scout_luck += v; break;
        case CE_BOSS_GUARD_PCT: m.boss_guard_pct += v; break;
        case CE_PHOENIX_ASH:    m.phoenix += lv; break;
        case CE_BLOOD_GRAIL:    m.dmg_pct += v; m.vamp_x10 += 25 * lv; break;
        case CE_STAR_CORE:      m.star_core += lv; m.rage_pct += 20 * lv; m.ult_pct += 25 * lv; break;
        case CE_CROWN_ALL:      m.dmg_pct += v; m.hp_pct += v; m.spd_pct += 10 * lv; m.shield_pct += 10 * lv; break;
        }
    }
    if (m.hp_pct < 45) m.hp_pct = 45;
    if (m.chip_res_pct > 80) m.chip_res_pct = 80;
    if (m.boss_guard_pct > 60) m.boss_guard_pct = 60;
    if (m.crit_pct > 90) m.crit_pct = 90;
    if (m.box_disc_pct > 50) m.box_disc_pct = 50;
}

void start_new_run()
{
    st.crystals = 0.0;
    st.boxes_opened = 0;
    st.free_boxes = 0;
    st.wave_reached = 1;
    st.map_lane = 1;
    memset(st.party, 0, sizeof(st.party));
    st.party[0] = {true, 0, 1}; /* #00 LUMI ★1 (弱い初期スタート) */
    memset(st.charm_lv, 0, sizeof(st.charm_lv));
    st.draft_open = false;
    memset(st.draft, 0, sizeof(st.draft));
    st.swap_talent = -1;
    st.phoenix_used = false;
    st.last_unlock_t = 0;
    st.last_unlock_c = 0;

    st.run_number++;
    st.discovered_talents |= 1ULL;
    apply_unlock_tier(unlock_tier_for_progress(st.best_wave, st.run_number, st.total_clears > 0));
    recalc_mods();
    save();
}

void hard_reset()
{
    memset(&st, 0, sizeof(st));
    s_buff_until_ms = 0;
    start_new_run(); /* sets run_number = 1, wave = 1, solo LUMI *1 */
    Serial.println("[game] hard reset (Run #1)");
}

void init()
{
    memset(&st, 0, sizeof(st));
    s_buff_until_ms = 0;

    prefs.begin("garden", true);
    SavePOD p{};
    size_t got = prefs.getBytes("save4", &p, sizeof(p));
    prefs.end();

    if (got == sizeof(p) && p.magic == 0x4C554D49u && p.ver == 4) {
        st.crystals = (p.crystals >= 0 && p.crystals < 1e9) ? p.crystals : 0.0;
        st.boxes_opened = p.boxes_opened;
        st.free_boxes = p.free_boxes;
        st.wave_reached = (p.wave_reached >= 1 && p.wave_reached <= 50) ? p.wave_reached : 1;
        st.map_lane = 1;
        memcpy(st.party, p.party, sizeof(st.party));
        for (int i = 0; i < PARTY_MAX; ++i) {
            if (st.party[i].talent_id >= TALENT_N) st.party[i].active = false;
            if (st.party[i].active && st.party[i].rank == 0) st.party[i].rank = 1;
        }
        if (!st.party[0].active && !st.party[1].active && !st.party[2].active) {
            st.party[0] = {true, 0, 1};
        }
        for (int i = 0; i < CHARM_N; ++i) {
            st.charm_lv[i] = (p.charm_lv[i] <= CHARMS[i].max_lv) ? p.charm_lv[i] : CHARMS[i].max_lv;
        }
        st.draft_open = (p.draft_open != 0);
        memcpy(st.draft, p.draft, sizeof(st.draft));
        for (int i = 0; i < 3; ++i) {
            if (st.draft[i].is_talent && st.draft[i].id >= TALENT_N) st.draft_open = false;
            if (!st.draft[i].is_talent && st.draft[i].id >= CHARM_N) st.draft_open = false;
        }
        st.swap_talent = -1;
        st.phoenix_used = (p.phoenix_used != 0);
        st.pet_count = p.pet_count;
        st.run_number = p.run_number ? p.run_number : 1;
        st.total_clears = p.total_clears;
        st.best_wave = p.best_wave;
        st.bosses_slain = p.bosses_slain;
        st.unlock_tier = (p.unlock_tier <= 8) ? p.unlock_tier : 0;
        st.unlocked_talents = p.unlocked_talents | 1ULL;
        st.discovered_talents = p.discovered_talents | 1ULL;
        st.unlocked_charms = p.unlocked_charms;
        apply_unlock_tier(st.unlock_tier);
        recalc_mods();
        Serial.printf("[game] roguelite save loaded: Run#%u Wave %u\r\n",
                      (unsigned)st.run_number, (unsigned)st.wave_reached);
    } else {
        hard_reset();
    }
}

void on_run_end(uint32_t wave, bool cleared)
{
    if (wave > st.best_wave) st.best_wave = wave;
    if (cleared) st.total_clears++;
    uint8_t new_tier = unlock_tier_for_progress(st.best_wave, st.run_number + 1, st.total_clears > 0);
    uint8_t nt = 0, nc = 0;
    apply_unlock_tier(new_tier, &nt, &nc);
    st.last_unlock_t = nt;
    st.last_unlock_c = nc;
    save();
}

void on_boss_slain(uint32_t wave)
{
    st.bosses_slain++;
    if (wave < 50) {
        if (st.free_boxes < 9) st.free_boxes++;
    }
    if (wave > st.best_wave) st.best_wave = wave;
    uint8_t new_tier = unlock_tier_for_progress(st.best_wave, st.run_number, st.total_clears > 0);
    apply_unlock_tier(new_tier);
    save();
}

/* ---------- 経済・ドロップ (放置収入ゼロ) ---------- */
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
void tick(double) { /* ローグライト仕様: 放置収入なし */ }

void add_drops(double raw_crystals, int party_scavenger_bonus)
{
    double mult = pet_mult() *
                  ((st.mods.drop_pct + party_scavenger_bonus) / 100.0) *
                  (gp::g.drop_pct / 100.0) * 1.05;
    st.crystals += raw_crystals * mult;
}

void debug_give(double v)
{
    st.crystals += v;
    if (st.crystals < 0.0) st.crystals = 0.0;
}

/* ---------- ルートボックス (3択ドラフト) ---------- */
double box_cost()
{
    double n = (double)st.boxes_opened;
    double raw = 35.0 + n * 24.0 + pow(n, 1.46) * 8.5;
    double disc = st.mods.box_disc_pct / 100.0;
    double mult = 1.0 - disc;
    if (mult < 0.35) mult = 0.35;
    mult *= (gp::g.box_cost_pct / 100.0);
    double res = floor(raw * mult + 0.5);
    return (res < 1.0) ? 1.0 : res;
}

bool can_open_box()
{
    if (st.draft_open) return false;
    return st.free_boxes > 0 || st.crystals >= box_cost();
}

static uint8_t roll_rarity(uint32_t wave, int scout_luck)
{
    float luck = wave * 0.25f + scout_luck * 0.45f;
    float w_ssr = 2.8f + luck * 0.14f;
    float w_sr  = 10.5f + luck * 0.35f;
    float w_r   = 27.0f + luck * 0.45f;
    float w_n   = 100.0f - (w_ssr + w_sr + w_r);
    if (w_n < 25.0f) w_n = 25.0f;
    float r = rl_frand01() * (w_n + w_r + w_sr + w_ssr);
    if (r < w_n) return RAR_N;
    r -= w_n;
    if (r < w_r) return RAR_R;
    r -= w_r;
    if (r < w_sr) return RAR_SR;
    return RAR_SSR;
}

bool open_lootbox(bool force_free)
{
    if (st.draft_open) return false;
    if (!force_free) {
        if (st.free_boxes > 0) {
            st.free_boxes--;
        } else {
            double cost = box_cost();
            if (st.crystals < cost) return false;
            st.crystals -= cost;
            st.boxes_opened++;
        }
    }

    uint64_t seen_t = 0, seen_c = 0;
    int pcnt = party_count();
    int valid_cards = 0;

    for (int slot = 0; slot < 3; ++slot) {
        bool want_talent = (st.boxes_opened <= 2 && pcnt < 3 && slot == 0) ||
                           ((rl_rand() % 100) < (pcnt < 3 ? 56u : 36u));

        bool found = false;
        if (want_talent) {
            for (int tries = 0; tries < 10 && !found; ++tries) {
                uint8_t rar = roll_rarity(st.wave_reached, st.mods.scout_luck);
                uint8_t cands[TALENT_N];
                int nc = 0;
                for (int i = 0; i < TALENT_N; ++i) {
                    if ((st.unlocked_talents & (1ULL << i)) && !(seen_t & (1ULL << i)) && TALENTS[i].rarity == rar) {
                        cands[nc++] = (uint8_t)i;
                    }
                }
                if (nc == 0 && tries >= 5) {
                    for (int i = 0; i < TALENT_N; ++i) {
                        if ((st.unlocked_talents & (1ULL << i)) && !(seen_t & (1ULL << i))) {
                            cands[nc++] = (uint8_t)i;
                        }
                    }
                }
                if (nc > 0) {
                    uint8_t id = cands[rl_rand() % nc];
                    seen_t |= (1ULL << id);
                    st.draft[valid_cards++] = {true, id, TALENTS[id].rarity};
                    found = true;
                }
            }
        }
        if (!found) {
            for (int tries = 0; tries < 10 && !found; ++tries) {
                uint8_t rar = roll_rarity(st.wave_reached, st.mods.scout_luck);
                uint8_t cands[CHARM_N];
                int nc = 0;
                for (int i = 0; i < CHARM_N; ++i) {
                    if ((st.unlocked_charms & (1ULL << i)) && !(seen_c & (1ULL << i)) &&
                        st.charm_lv[i] < CHARMS[i].max_lv && CHARMS[i].rarity == rar) {
                        cands[nc++] = (uint8_t)i;
                    }
                }
                if (nc == 0 && tries >= 5) {
                    for (int i = 0; i < CHARM_N; ++i) {
                        if ((st.unlocked_charms & (1ULL << i)) && !(seen_c & (1ULL << i)) &&
                            st.charm_lv[i] < CHARMS[i].max_lv) {
                            cands[nc++] = (uint8_t)i;
                        }
                    }
                }
                if (nc > 0) {
                    uint8_t id = cands[rl_rand() % nc];
                    seen_c |= (1ULL << id);
                    st.draft[valid_cards++] = {false, id, CHARMS[id].rarity};
                    found = true;
                }
            }
        }
        if (!found) {
            /* 万一全チャーム上限ならキャラへフォールバック */
            uint8_t cands[TALENT_N];
            int nc = 0;
            for (int i = 0; i < TALENT_N; ++i) {
                if ((st.unlocked_talents & (1ULL << i)) && !(seen_t & (1ULL << i))) cands[nc++] = (uint8_t)i;
            }
            if (nc > 0) {
                uint8_t id = cands[rl_rand() % nc];
                seen_t |= (1ULL << id);
                st.draft[valid_cards++] = {true, id, TALENTS[id].rarity};
            }
        }
    }

    st.draft_open = (valid_cards > 0);
    st.swap_talent = -1;
    save();
    return st.draft_open;
}

int pick_draft(int card_idx, int swap_slot)
{
    if (!st.draft_open || card_idx < 0 || card_idx >= 3) return 0;
    const DraftCard dc = st.draft[card_idx];

    if (dc.is_talent) {
        int tid = dc.id;
        for (int i = 0; i < PARTY_MAX; ++i) {
            if (st.party[i].active && st.party[i].talent_id == tid) {
                if (st.party[i].rank < 9) st.party[i].rank++;
                st.discovered_talents |= (1ULL << tid);
                st.draft_open = false;
                st.swap_talent = -1;
                recalc_mods();
                save();
                return 2; /* RANK UP */
            }
        }
        for (int i = 0; i < PARTY_MAX; ++i) {
            if (!st.party[i].active) {
                st.party[i] = {true, (uint8_t)tid, 1};
                st.discovered_talents |= (1ULL << tid);
                st.draft_open = false;
                st.swap_talent = -1;
                recalc_mods();
                save();
                return 1; /* HIRED */
            }
        }
        if (swap_slot >= 0 && swap_slot < PARTY_MAX) {
            st.party[swap_slot] = {true, (uint8_t)tid, 1};
            st.discovered_talents |= (1ULL << tid);
            st.draft_open = false;
            st.swap_talent = -1;
            recalc_mods();
            save();
            return 3; /* SWAPPED */
        }
        st.swap_talent = (int8_t)tid;
        return 5; /* NEED SWAP SLOT */
    } else {
        int cid = dc.id;
        if (cid >= 0 && cid < CHARM_N && st.charm_lv[cid] < CHARMS[cid].max_lv) {
            st.charm_lv[cid]++;
        }
        st.draft_open = false;
        st.swap_talent = -1;
        recalc_mods();
        save();
        return 4; /* CHARM */
    }
}

bool debug_grant_talent(int tid, int slot_hint)
{
    if (tid < 0 || tid >= TALENT_N) return false;
    for (int i = 0; i < PARTY_MAX; ++i) {
        if (st.party[i].active && st.party[i].talent_id == tid) {
            if (st.party[i].rank < 9) st.party[i].rank++;
            st.discovered_talents |= (1ULL << tid);
            recalc_mods();
            save();
            return true;
        }
    }
    int dst = -1;
    for (int i = 0; i < PARTY_MAX; ++i) {
        if (!st.party[i].active) { dst = i; break; }
    }
    if (dst < 0) dst = (slot_hint >= 0 && slot_hint < PARTY_MAX) ? slot_hint : (PARTY_MAX - 1);
    st.party[dst] = {true, (uint8_t)tid, 1};
    st.discovered_talents |= (1ULL << tid);
    recalc_mods();
    save();
    return true;
}

bool debug_grant_charm(int cid)
{
    if (cid < 0 || cid >= CHARM_N) return false;
    if (st.charm_lv[cid] < CHARMS[cid].max_lv) st.charm_lv[cid]++;
    recalc_mods();
    save();
    return true;
}

void unlock_all_meta()
{
    apply_unlock_tier(8);
    st.discovered_talents = 0xFFFFFFFFFFFFFFFFULL;
    save();
}

int party_count()
{
    int c = 0;
    for (int i = 0; i < PARTY_MAX; ++i) if (st.party[i].active) c++;
    return c;
}

int total_charms_count()
{
    int c = 0;
    for (int i = 0; i < CHARM_N; ++i) c += st.charm_lv[i];
    return c;
}

int unlocked_talent_count()
{
    int c = 0;
    for (int i = 0; i < TALENT_N; ++i) if (st.unlocked_talents & (1ULL << i)) c++;
    return c;
}

int discovered_talent_count()
{
    int c = 0;
    for (int i = 0; i < TALENT_N; ++i) if (st.discovered_talents & (1ULL << i)) c++;
    return c;
}

int unlocked_charm_count()
{
    int c = 0;
    for (int i = 0; i < CHARM_N; ++i) if (st.unlocked_charms & (1ULL << i)) c++;
    return c;
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

/* ---------- STS / Peglin 式ルートマップ生成 ---------- */
static uint32_t map_hash(uint32_t step, uint32_t salt)
{
    uint32_t h = (st.run_number * 2654435761u) ^ (step * 2246822519u) ^ (salt * 3266489917u) ^ 0x51A7E123u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h;
}

NodeKind map_node_kind(uint32_t step, int lane)
{
    if (lane < 0) lane = 0;
    if (lane > 2) lane = 2;
    if (step <= 1) return NODE_BATTLE;
    if (step >= 50 || (step % 5) == 0) return NODE_BOSS;

    uint32_t mod = step % 5;
    uint32_t perm = map_hash(step, 1) % 6;
    /* 3レーンに異なる種類を割り当てるための6通り置換テーブル */
    static const uint8_t PERM[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    uint8_t slot = PERM[perm][lane];

    if (mod == 4) {
        /* ボス直前: 必ず CAMP / ELITE / (TREASURE or EVENT) の究極の3択 */
        if (slot == 0) return NODE_CAMP;
        if (slot == 1) return NODE_ELITE;
        return (map_hash(step, 7) & 1) ? NODE_TREASURE : NODE_EVENT;
    }
    if (mod == 3) {
        /* 中盤分岐: ELITE / EVENT / (TREASURE or BATTLE) */
        if (slot == 0) return NODE_ELITE;
        if (slot == 1) return NODE_EVENT;
        return (map_hash(step, 9) & 1) ? NODE_TREASURE : NODE_BATTLE;
    }
    if (mod == 2) {
        /* 序盤分岐: BATTLE / EVENT / (CAMP or TREASURE) */
        if (slot == 0) return NODE_BATTLE;
        if (slot == 1) return NODE_EVENT;
        return (map_hash(step, 11) % 3 == 0) ? NODE_TREASURE : NODE_CAMP;
    }
    /* mod == 1 (ボス突破直後の新エリア入口): BATTLE / BATTLE / EVENT */
    if (slot == 2) return NODE_EVENT;
    return NODE_BATTLE;
}

bool map_edge_exists(uint32_t from_step, int from_lane, int to_lane)
{
    if (from_lane < 0 || from_lane > 2 || to_lane < 0 || to_lane > 2) return false;
    uint32_t next_step = from_step + 1;
    if (next_step > 50) return false;
    if (next_step % 5 == 0) return to_lane == 1;          /* ボスへ収束 */
    if (from_step <= 1 || from_step % 5 == 0) return true; /* ボス直後・Wave1後は全3レーンへ分岐 */
    /* 通常ステップ: 同レーンまたは隣接レーンへ進行可能 */
    int d = to_lane - from_lane;
    return (d >= -1 && d <= 1);
}

const char* map_node_name(NodeKind k)
{
    switch (k) {
    case NODE_BATTLE:   return "BATTLE";
    case NODE_ELITE:    return "ELITE";
    case NODE_CAMP:     return "CAMP";
    case NODE_TREASURE: return "CHEST";
    case NODE_EVENT:    return "EVENT";
    case NODE_BOSS:     return "BOSS";
    default:            return "BATTLE";
    }
}

const char* chapter_name(uint32_t step)
{
    if (step <= 10) return "CH1 GARDEN";
    if (step <= 20) return "CH2 TWILIGHT";
    if (step <= 30) return "CH3 CRIMSON";
    if (step <= 40) return "CH4 ASTRAL";
    return "CH5 THRONE";
}

void grant_free_box(uint8_t n)
{
    uint16_t v = (uint16_t)st.free_boxes + n;
    st.free_boxes = (v > 9) ? 9 : (uint8_t)v;
    save();
}

int grant_random_charm()
{
    int pool[CHARM_N], n = 0;
    for (int i = 0; i < CHARM_N; ++i) {
        if ((st.unlocked_charms & (1ULL << i)) && st.charm_lv[i] < CHARMS[i].max_lv) {
            pool[n++] = i;
        }
    }
    if (n == 0) return -1;
    int cid = pool[rl_rand() % (uint32_t)n];
    st.charm_lv[cid]++;
    recalc_mods();
    save();
    return cid;
}

} // namespace game
