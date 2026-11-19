#!/usr/bin/env python3
"""
Monte Carlo 50-Wave Roguelite Balance Simulator for Crystal Garden v4.0.
Parses TALENTS[64], CHARMS[36], and BOSSES[6] directly from src/roguelite_data.h
and tunes / verifies an ~1.0% (0.8% - 1.4%) Wave 50 clear rate.
"""
import argparse
import math
import random
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA_H = ROOT / "src" / "roguelite_data.h"


def parse_header():
    txt = DATA_H.read_text(encoding="utf-8")
    talents_block = re.search(r"TALENTS\[TALENT_N\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    talents = []
    pat_t = re.compile(
        r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(RAR_\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*'
        r'C565\([^)]+\),\s*C565\([^)]+\),\s*(STYLE_\w+),\s*(TRAIT_\w+),\s*(ULT_\w+),\s*'
        r'([\d.]+),\s*([\d.]+)f?,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}'
    )
    rar_map = {"RAR_N": 0, "RAR_R": 1, "RAR_SR": 2, "RAR_SSR": 3}
    for idx, m in enumerate(pat_t.finditer(talents_block)):
        talents.append({
            "id": idx,
            "name": m.group(1),
            "role": m.group(2),
            "rarity": rar_map[m.group(4)],
            "unlock_tier": int(m.group(5)),
            "style": m.group(8),
            "trait": m.group(9),
            "ult": m.group(10),
            "hp": float(m.group(11)),
            "dmg": float(m.group(12)),
            "spd": int(m.group(13)) / 100.0,
            "reach": int(m.group(14)),
            "cycle_frames": int(m.group(15)) + int(m.group(16)) + int(m.group(17)),
        })

    charms_block = re.search(r"CHARMS\[CHARM_N\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    charms = []
    pat_c = re.compile(
        r'\{"([^"]+)",\s*"([^"]+)",\s*(RAR_\w+),\s*(\d+),\s*(\d+),\s*(CE_\w+),\s*(-?\d+)\}'
    )
    for idx, m in enumerate(pat_c.finditer(charms_block)):
        charms.append({
            "id": idx,
            "name": m.group(1),
            "rarity": rar_map[m.group(3)],
            "unlock_tier": int(m.group(4)),
            "max_lv": int(m.group(5)),
            "eff": m.group(6),
            "val": int(m.group(7)),
        })

    bosses_block = re.search(r"BOSSES\[BOSS_N\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    bosses = []
    pat_b = re.compile(
        r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*'
        r'(\d+),\s*(\d+),\s*C565\([^)]+\),\s*C565\([^)]+\),\s*(\d+),\s*([\d.]+)f?,\s*(\d+),\s*(\d+),\s*(\d+)\}'
    )
    for idx, m in enumerate(pat_b.finditer(bosses_block)):
        bosses.append({
            "id": idx,
            "name": f"{m.group(1)} {m.group(2)}",
            "hp_base": float(m.group(8)),
            "dmg_mult": float(m.group(9)),
        })

    assert len(talents) == 64
    assert len(charms) == 36
    assert len(bosses) == 6
    return talents, charms, bosses


# Tuned parameters (mirrored into C++ beat.cpp / garden_game.cpp) -> ~1.0% Wave 50 clear rate
PARAMS = {
    "ehp_lin": 0.25,
    "ehp_quad": 0.0190,
    "edmg_lin": 0.115,
    "edmg_quad": 0.0075,
    "enemy_dps_base": 3.4,    # Direct enemy contact DPS at Wave 1 (~3.4/s before wave scaling)
    "boss_hp_scale": 0.90,
    "boss_phase_spike": 1.15,
    "chip_base": 1.35,
    "drop_base": 1.05,
    "box_base": 35.0,
    "box_lin": 24.0,
    "box_exp_k": 8.5,
    "box_exp": 1.46,
    "wave_heal_pct": 0.12,
    "boss_heal_pct": 0.45,
}


def box_cost(boxes_opened, disc_pct, p=PARAMS):
    raw = p["box_base"] + boxes_opened * p["box_lin"] + (boxes_opened ** p["box_exp"]) * p["box_exp_k"]
    mult = max(0.35, 1.0 - disc_pct / 100.0)
    return math.floor(raw * mult)


def unlock_tier_for_progress(best_wave, runs, cleared):
    if cleared:
        return 8
    t = 0
    if best_wave >= 4 or runs >= 2: t = 1
    if best_wave >= 8 or runs >= 4: t = 2
    if best_wave >= 12 or runs >= 6: t = 3
    if best_wave >= 16 or runs >= 8: t = 4
    if best_wave >= 22 or runs >= 11: t = 5
    if best_wave >= 28 or runs >= 15: t = 6
    if best_wave >= 35 or runs >= 20: t = 7
    if best_wave >= 42 or runs >= 28: t = 8
    return t


def calc_mods(charm_lvs, charms):
    m = {
        "dmg_pct": 100, "hp_pct": 100, "crit_pct": 5, "spd_pct": 100,
        "vamp_x10": 0, "regen_x10": 0, "chip_res_pct": 0, "flat_armor": 0,
        "drop_pct": 100, "box_disc_pct": 0, "rage_pct": 100, "ult_pct": 100,
        "wave_heal_pct": 0, "boss_slay_pct": 0, "exec_pct": 0, "thorns": 0,
        "shield_pct": 0, "start_mp": 0, "berserk_pct": 0, "chain_spark": 0,
        "frost_slow": 0, "twin_bolt": 0, "kill_heal_pct": 0, "cleanse_ult": 0,
        "scout_luck": 0, "boss_guard_pct": 0, "phoenix": 0, "star_core": 0,
    }
    for cid, lv in enumerate(charm_lvs):
        if lv <= 0:
            continue
        c = charms[cid]
        v = c["val"] * lv
        eff = c["eff"]
        if eff == "CE_ATK_PCT": m["dmg_pct"] += v
        elif eff == "CE_HP_PCT": m["hp_pct"] += v
        elif eff == "CE_CRIT_PCT": m["crit_pct"] += v
        elif eff == "CE_SPD_PCT": m["spd_pct"] += v
        elif eff == "CE_VAMP_X10": m["vamp_x10"] += v
        elif eff == "CE_REGEN_X10": m["regen_x10"] += v
        elif eff == "CE_CHIP_RES_PCT": m["chip_res_pct"] += v
        elif eff == "CE_FLAT_ARMOR": m["flat_armor"] += v
        elif eff == "CE_DROP_PCT": m["drop_pct"] += v
        elif eff == "CE_BOX_DISC_PCT": m["box_disc_pct"] += v
        elif eff == "CE_RAGE_PCT": m["rage_pct"] += v
        elif eff == "CE_ULT_PCT": m["ult_pct"] += v
        elif eff == "CE_WAVE_HEAL_PCT": m["wave_heal_pct"] += v
        elif eff == "CE_BOSS_SLAY_PCT": m["boss_slay_pct"] += v
        elif eff == "CE_EXEC_PCT": m["exec_pct"] += v
        elif eff == "CE_THORNS": m["thorns"] += v
        elif eff == "CE_SHIELD_PCT": m["shield_pct"] += v
        elif eff == "CE_START_MP": m["start_mp"] += v
        elif eff == "CE_GLASS_ATK": m["dmg_pct"] += v; m["hp_pct"] -= 16 * lv
        elif eff == "CE_BERSERK_PCT": m["berserk_pct"] += v
        elif eff == "CE_CHAIN_SPARK": m["chain_spark"] += v
        elif eff == "CE_FROST_SLOW": m["frost_slow"] += v
        elif eff == "CE_TWIN_BOLT": m["twin_bolt"] += v
        elif eff == "CE_KILL_HEAL_PCT": m["kill_heal_pct"] += v
        elif eff == "CE_CLEANSE_ULT": m["cleanse_ult"] += lv
        elif eff == "CE_SCOUT_LUCK": m["scout_luck"] += v
        elif eff == "CE_BOSS_GUARD_PCT": m["boss_guard_pct"] += v
        elif eff == "CE_PHOENIX_ASH": m["phoenix"] += lv
        elif eff == "CE_BLOOD_GRAIL": m["dmg_pct"] += v; m["vamp_x10"] += 25 * lv
        elif eff == "CE_STAR_CORE": m["star_core"] += lv; m["rage_pct"] += 20 * lv; m["ult_pct"] += 25 * lv
        elif eff == "CE_CROWN_ALL": m["dmg_pct"] += v; m["hp_pct"] += v; m["spd_pct"] += 10 * lv; m["shield_pct"] += 10 * lv
    m["hp_pct"] = max(45, m["hp_pct"])
    m["chip_res_pct"] = min(80, m["chip_res_pct"])
    m["boss_guard_pct"] = min(60, m["boss_guard_pct"])
    m["crit_pct"] = min(90, m["crit_pct"])
    m["box_disc_pct"] = min(50, m["box_disc_pct"])
    return m


def roll_rarity(rng, wave, scout_luck):
    luck_boost = wave * 0.25 + scout_luck * 0.45
    w_ssr = 2.8 + luck_boost * 0.14
    w_sr = 10.5 + luck_boost * 0.35
    w_r = 27.0 + luck_boost * 0.45
    w_n = max(25.0, 100.0 - (w_ssr + w_sr + w_r))
    r = rng.random() * (w_n + w_r + w_sr + w_ssr)
    if r < w_n: return 0
    r -= w_n
    if r < w_r: return 1
    r -= w_r
    if r < w_sr: return 2
    return 3


def roll_draft(rng, talents, charms, charm_lvs, party, unlock_tier, wave, boxes_opened, mods):
    cards = []
    seen_t, seen_c = set(), set()
    party_count = len(party)
    for slot_idx in range(3):
        want_talent = (boxes_opened < 2 and party_count < 3 and slot_idx == 0) or (
            rng.random() < (0.56 if party_count < 3 else 0.36)
        )
        chosen = None
        if want_talent:
            for _ in range(12):
                rar = roll_rarity(rng, wave, mods["scout_luck"])
                pool = [t for t in talents if t["unlock_tier"] <= unlock_tier and t["rarity"] == rar and t["id"] not in seen_t]
                if not pool:
                    pool = [t for t in talents if t["unlock_tier"] <= unlock_tier and t["id"] not in seen_t]
                if pool:
                    pick = rng.choice(pool)
                    seen_t.add(pick["id"])
                    chosen = ("T", pick["id"], pick["rarity"])
                    break
        if chosen is None:
            for _ in range(12):
                rar = roll_rarity(rng, wave, mods["scout_luck"])
                pool = [
                    c for c in charms
                    if c["unlock_tier"] <= unlock_tier and c["rarity"] == rar
                    and charm_lvs[c["id"]] < c["max_lv"] and c["id"] not in seen_c
                ]
                if not pool:
                    pool = [
                        c for c in charms
                        if c["unlock_tier"] <= unlock_tier
                        and charm_lvs[c["id"]] < c["max_lv"] and c["id"] not in seen_c
                    ]
                if pool:
                    pick = rng.choice(pool)
                    seen_c.add(pick["id"])
                    chosen = ("C", pick["id"], pick["rarity"])
                    break
        if chosen:
            cards.append(chosen)
    return cards


def smart_bot_pick(cards, party, charm_lvs, talents, charms, mods, rng):
    best_idx = 0
    best_score = -1e9
    party_ids = {p["id"]: i for i, p in enumerate(party)}
    has_healer = any(talents[p["id"]]["style"] in ("STYLE_ALCH_MEND", "STYLE_PALADIN_WARD") for p in party)
    party_count = len(party)

    for idx, (kind, cid, rar) in enumerate(cards):
        sc = 0.0
        if kind == "T":
            t = talents[cid]
            power = t["hp"] * 0.25 + t["dmg"] * 4.5 + rar * 24.0
            if cid in party_ids:
                sc = power * 1.15 + 32.0
            elif party_count < 3:
                sc = power * 1.55 + (42.0 if not has_healer and t["style"] in ("STYLE_ALCH_MEND", "STYLE_PALADIN_WARD") else 0.0)
            else:
                weakest_i = min(range(3), key=lambda i: talents[party[i]["id"]]["rarity"] * 40 + party[i]["rank"] * 30 + talents[party[i]["id"]]["dmg"] * 3)
                weak_sc = talents[party[weakest_i]["id"]]["rarity"] * 40 + party[weakest_i]["rank"] * 30 + talents[party[weakest_i]["id"]]["dmg"] * 3
                new_sc = rar * 40 + 30 + t["dmg"] * 3
                sc = (new_sc - weak_sc) * 1.1 + 10.0
        else:
            c = charms[cid]
            eff = c["eff"]
            sc = 28.0 + rar * 22.0 + c["val"] * 0.65
            if eff in ("CE_ATK_PCT", "CE_BLOOD_GRAIL", "CE_CROWN_ALL", "CE_STAR_CORE"):
                sc += 28.0
            elif eff in ("CE_VAMP_X10", "CE_CHIP_RES_PCT", "CE_REGEN_X10", "CE_FLAT_ARMOR", "CE_PHOENIX_ASH", "CE_KILL_HEAL_PCT"):
                sc += 36.0 if (mods["vamp_x10"] < 15 or mods["chip_res_pct"] < 35) else 16.0
            elif eff == "CE_GLASS_ATK":
                sc += 18.0 if mods["hp_pct"] >= 110 else -8.0
            else:
                sc += 20.0
        sc += rng.uniform(-3.0, 3.0)
        if sc > best_score:
            best_score = sc
            best_idx = idx

    kind, cid, rar = cards[best_idx]
    if kind == "T":
        if cid in party_ids:
            slot = party[party_ids[cid]]
            slot["rank"] += 1
            slot["hp_frac"] = min(1.0, slot["hp_frac"] + 0.35)
        elif len(party) < 3:
            party.append({"id": cid, "rank": 1, "hp_frac": 1.0})
        else:
            weakest_i = min(range(3), key=lambda i: talents[party[i]["id"]]["rarity"] * 40 + party[i]["rank"] * 30 + talents[party[i]["id"]]["dmg"] * 3)
            party[weakest_i] = {"id": cid, "rank": 1, "hp_frac": 1.0}
    else:
        charm_lvs[cid] += 1


def simulate_run(rng, talents, charms, bosses, unlock_tier, p):
    party = [{"id": 0, "rank": 1, "hp_frac": 1.0}]
    charm_lvs = [0] * len(charms)
    crystals = 0.0
    boxes_opened = 0
    phoenix_used = False
    max_wave_cleared = 0

    for w in range(1, 51):
        mods = calc_mods(charm_lvs, charms)
        boss_wave = (w % 5 == 0)

        while True:
            cost = box_cost(boxes_opened, mods["box_disc_pct"], p)
            if crystals >= cost:
                crystals -= cost
                boxes_opened += 1
                cards = roll_draft(rng, talents, charms, charm_lvs, party, unlock_tier, w, boxes_opened - 1, mods)
                if cards:
                    smart_bot_pick(cards, party, charm_lvs, talents, charms, mods, rng)
                    mods = calc_mods(charm_lvs, charms)
            else:
                break

        total_hpmax = 0.0
        cur_hp = 0.0
        team_dps = 0.0
        team_sustain_hps = 0.0
        warden_bonus = 0
        scavenger_bonus = 0
        thorns_dps = mods["thorns"] * 0.9

        for slot in party:
            t = talents[slot["id"]]
            rm = 1.0 + 0.28 * (slot["rank"] - 1)
            mhp = t["hp"] * rm * (mods["hp_pct"] / 100.0)
            shield = 0.0
            if mods["shield_pct"] > 0:
                shield += mhp * (mods["shield_pct"] / 100.0)
            if t["trait"] == "TRAIT_BARRIER":
                shield += mhp * 0.18
            total_hpmax += mhp
            cur_hp += mhp * slot["hp_frac"] + shield

            crit = min(0.95, (mods["crit_pct"] + (15 if t["trait"] == "TRAIT_CRIT" else 0)) / 100.0)
            crit_mult = 1.0 + crit
            spd_mult = (mods["spd_pct"] / 100.0) * (1.18 if t["trait"] == "TRAIT_HASTE" else 1.0)
            hits_per_sec = min(4.2, (60.0 / max(10, t["cycle_frames"] + 6)) * spd_mult)
            if t["style"] in ("STYLE_COMBO_BLADE", "STYLE_FLURRY_DART"):
                hits_per_sec *= 1.18
            elif t["style"] == "STYLE_SPREAD_VOLLEY":
                hits_per_sec *= 1.28

            dmg_per_hit = t["dmg"] * rm * (mods["dmg_pct"] / 100.0) * crit_mult
            if t["trait"] == "TRAIT_BERSERK": dmg_per_hit *= 1.12
            if mods["berserk_pct"] > 0: dmg_per_hit *= (1.0 + mods["berserk_pct"] / 250.0)
            if t["trait"] == "TRAIT_EXEC": dmg_per_hit *= (1.30 if boss_wave else 1.15)
            if boss_wave and mods["boss_slay_pct"] > 0: dmg_per_hit *= (1.0 + mods["boss_slay_pct"] / 100.0)
            if mods["exec_pct"] > 0: dmg_per_hit *= (1.0 + mods["exec_pct"] / 240.0)
            if mods["twin_bolt"] > 0 and t["style"] in ("STYLE_MAGIC_BOLT", "STYLE_SPREAD_VOLLEY", "STYLE_SNIPER_LANCE", "STYLE_ALCH_MEND"):
                dmg_per_hit *= (1.0 + 0.55 * (mods["twin_bolt"] / 100.0))
            if mods["chain_spark"] > 0:
                dmg_per_hit *= (1.0 + 0.38 * (mods["chain_spark"] / 100.0))

            rage_rate = hits_per_sec * 7.5 * (mods["rage_pct"] / 100.0) * (1.3 if t["trait"] == "TRAIT_RAGE" else 1.0)
            ult_freq = rage_rate / 100.0
            ult_mult = (mods["ult_pct"] / 100.0) * (1.35 if mods["star_core"] > 0 else 1.0)
            if t["ult"] == "ULT_SANCTUARY":
                team_sustain_hps += ult_freq * mhp * 0.42 * ult_mult
                ult_dps = ult_freq * dmg_per_hit * 2.5
            else:
                ult_dps = ult_freq * dmg_per_hit * 6.8 * ult_mult

            team_dps += hits_per_sec * dmg_per_hit + ult_dps
            vamp = mods["vamp_x10"] / 10.0 + (2.4 if t["trait"] == "TRAIT_VAMP" else 0.0)
            team_sustain_hps += hits_per_sec * vamp
            if t["style"] == "STYLE_ALCH_MEND":
                team_sustain_hps += hits_per_sec * (3.2 + 0.4 * slot["rank"])
            elif t["style"] == "STYLE_PALADIN_WARD":
                team_sustain_hps += hits_per_sec * (1.8 + 0.3 * slot["rank"])
            if t["trait"] == "TRAIT_WARDEN": warden_bonus = 35
            if t["trait"] == "TRAIT_SCAVENGER": scavenger_bonus = 20
            if t["trait"] == "TRAIT_THORNS": thorns_dps += 6.0 * rm

        team_sustain_hps += (mods["regen_x10"] / 10.0) * len(party)
        team_dps += thorns_dps * 0.55

        hp_mult = 1.0 + (w - 1) * p["ehp_lin"] + ((w - 1) ** 2) * p["ehp_quad"]
        dmg_mult = 1.0 + (w - 1) * p["edmg_lin"] + ((w - 1) ** 2) * p["edmg_quad"]

        n_minions = min(6, 3 + w // 4) if not boss_wave else 2
        total_enemy_hp = 0.0
        for mi in range(n_minions):
            ehp = 36.0 if (mi % 3 == 0) else (28.0 if mi % 3 == 1 else 86.0)
            total_enemy_hp += ehp * hp_mult

        boss_dmg_mult = 1.0
        if boss_wave:
            b = bosses[5 if w >= 50 else ((w // 5 - 1) % 5)]
            total_enemy_hp += b["hp_base"] * hp_mult * p["boss_hp_scale"] * (1.22 if w == 50 else 1.0)
            boss_dmg_mult = b["dmg_mult"]

        frost_factor = 0.90 if (mods["frost_slow"] > 0 or any(talents[s["id"]]["trait"] == "TRAIT_FROST" for s in party)) else 1.0
        clear_time = total_enemy_hp / max(5.0, team_dps)

        direct_dps = (p["enemy_dps_base"] + n_minions * 0.65 + (6.8 * boss_dmg_mult if boss_wave else 0.0)) * dmg_mult * frost_factor
        chip_res = min(0.85, (mods["chip_res_pct"] + warden_bonus) / 100.0)
        chip_dps = (1.6 + w * 0.25 + (3.6 if boss_wave else 0.0)) * dmg_mult * p["chip_base"] * (1.0 - chip_res)

        armor_reduce = mods["flat_armor"] * 1.45
        raw_inc = max(0.8, (direct_dps + chip_dps) - armor_reduce)
        net_loss = (raw_inc - team_sustain_hps) * clear_time

        if boss_wave:
            phase_guard = 1.0 - mods["boss_guard_pct"] / 100.0
            phase_burst = (12.0 + 1.8 * w) * boss_dmg_mult * dmg_mult * p["boss_phase_spike"] * phase_guard * (1.0 - chip_res * 0.45)
            slow_penalty = min(2.0, max(0.75, clear_time / 16.0))
            net_loss += phase_burst * slow_penalty * rng.uniform(0.82, 1.20)

        net_loss *= rng.uniform(0.90, 1.12)
        hp_after = cur_hp - net_loss
        if hp_after <= 0.0:
            if mods["phoenix"] > 0 and not phoenix_used:
                phoenix_used = True
                hp_after = total_hpmax * 0.50
            else:
                return max_wave_cleared, boxes_opened, False

        max_wave_cleared = w
        kill_heal = total_hpmax * (mods["kill_heal_pct"] / 100.0) * (n_minions * 0.5)
        wave_heal = total_hpmax * ((p["boss_heal_pct"] if boss_wave else p["wave_heal_pct"]) + mods["wave_heal_pct"] / 100.0)
        hp_after = min(total_hpmax, hp_after + kill_heal + wave_heal)
        avg_frac = max(0.15, min(1.0, hp_after / total_hpmax))
        for slot in party:
            slot["hp_frac"] = avg_frac

        drop_mult = (mods["drop_pct"] + scavenger_bonus) / 100.0 * p["drop_base"]
        kill_crystals = n_minions * (9.0 + w * 4.5)
        if boss_wave:
            kill_crystals += 75.0 + w * 18.0
        clear_bonus = (65.0 + w * 22.0) if boss_wave else (16.0 + w * 7.2)
        crystals += (kill_crystals + clear_bonus) * drop_mult

        if boss_wave and w < 50:
            cards = roll_draft(rng, talents, charms, charm_lvs, party, unlock_tier, w, boxes_opened, mods)
            if cards:
                smart_bot_pick(cards, party, charm_lvs, talents, charms, mods, rng)

    return 50, boxes_opened, True


def run_benchmark(p, n_full=4000, n_camp=40, seed=20261118):
    talents, charms, bosses = parse_header()
    rng = random.Random(seed)

    prog_clears = 0
    prog_waves = []
    prog_buckets = [0] * 6
    for _ in range(n_camp):
        best_w = 0
        cleared_ever = False
        for run_i in range(1, 101):
            tier = unlock_tier_for_progress(best_w, run_i, cleared_ever)
            w, _, win = simulate_run(rng, talents, charms, bosses, tier, p)
            if w > best_w: best_w = w
            prog_waves.append(w)
            if win:
                prog_clears += 1
                cleared_ever = True
            else:
                bi = 0 if w < 5 else (1 if w < 10 else (2 if w < 20 else (3 if w < 30 else (4 if w < 40 else 5))))
                prog_buckets[bi] += 1

    full_clears = 0
    full_waves = []
    full_buckets = [0] * 6
    for _ in range(n_full):
        w, _, win = simulate_run(rng, talents, charms, bosses, 8, p)
        full_waves.append(w)
        if win:
            full_clears += 1
        else:
            bi = 0 if w < 5 else (1 if w < 10 else (2 if w < 20 else (3 if w < 30 else (4 if w < 40 else 5))))
            full_buckets[bi] += 1

    return {
        "prog_rate": prog_clears / (n_camp * 100) * 100.0,
        "prog_avg_w": sum(prog_waves) / len(prog_waves),
        "prog_buckets": prog_buckets,
        "full_rate": full_clears / n_full * 100.0,
        "full_avg_w": sum(full_waves) / len(full_waves),
        "full_buckets": full_buckets,
    }


def sweep():
    talents, charms, bosses = parse_header()
    print("Sweeping curve parameters toward ~1% clear rate...")
    for edmg_q in [0.0065, 0.0078, 0.0090, 0.0102]:
        for ehp_q in [0.016, 0.020, 0.024]:
            for chip_b in [1.15, 1.35]:
                p = dict(PARAMS)
                p["edmg_lin"] = 0.115
                p["edmg_quad"] = edmg_q
                p["ehp_lin"] = 0.25
                p["ehp_quad"] = ehp_q
                p["chip_base"] = chip_b
                p["boss_phase_spike"] = 1.15
                res = run_benchmark(p, n_full=1200, n_camp=12, seed=99)
                print(
                    f"edmg_q={edmg_q:.4f} ehp_q={ehp_q:.3f} chip={chip_b:.2f} -> "
                    f"prog={res['prog_rate']:.2f}% (avgW={res['prog_avg_w']:.1f}) | "
                    f"full={res['full_rate']:.2f}% (avgW={res['full_avg_w']:.1f}) buckets={res['full_buckets']}"
                )


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--sweep", action="store_true")
    args = ap.parse_args()
    if args.sweep:
        sweep()
    else:
        res = run_benchmark(PARAMS, n_full=5000, n_camp=50)
        print(f"50x100 Fresh-Save Campaigns : clear={res['prog_rate']:.2f}% avgW={res['prog_avg_w']:.2f} wipes[0-4,5-9,10-19,20-29,30-39,40-49]={res['prog_buckets']}")
        print(f"5000 Steady-State Full Pool : clear={res['full_rate']:.2f}% avgW={res['full_avg_w']:.2f} wipes[0-4,5-9,10-19,20-29,30-39,40-49]={res['full_buckets']}")
