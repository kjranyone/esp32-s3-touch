/**
 * game_params.h — 調整パラメータの外部定義 (#046)
 *
 * シリアルコンソール (115200) から `キー 値` で実行中に書き換えられる。
 *   dmg_pct 125     → ダメージ倍率 125%
 *   ?               → 現在値をすべて表示
 *   reset           → 現ランをリスタート
 */
#pragma once
#include <stdint.h>

namespace gp {

struct Params {
    /* 味方強化倍率 (チャーム・なでなで等で変動) */
    uint32_t dmg_pct;          /* ダメージ倍率 (%) */
    uint32_t move_pct;         /* 移動速度倍率 (%) */
    uint32_t rage_pct;         /* 必殺技ゲージ蓄積倍率 (%) */
    uint32_t hp_pct;           /* 最大HP倍率 (%) */
    uint32_t crit_pct;         /* クリティカル率 (%) */
    uint32_t air_pct;          /* 空中攻撃倍率 (%) */
    uint32_t vamp_x10;         /* 攻撃時HP回復 x10 */
    uint32_t ult_pct;          /* 必殺技ダメージ倍率 (%) */
    uint32_t drop_pct;         /* ドロップ倍率 (%) */

    /* ローグライト難易度バランス係数 (%) */
    uint32_t enemy_hp_pct;     /* 敵HP倍率 (%) */
    uint32_t enemy_dmg_pct;    /* 敵攻撃力倍率 (%) */
    uint32_t chip_pct;         /* 床/毒/遠距離削り倍率 (%) */
    uint32_t box_cost_pct;     /* ルートボックス価格倍率 (%) */
    uint32_t wave_heal_pct;    /* 通常ウェーブ撃破時回復 (%) */

    /* なでなでバフ */
    uint32_t pet_buff_pct;     /* バフ率 (%) */
    uint32_t pet_duration_min; /* バフ持続 (分) */
    uint32_t hit_fx_frames;    /* ヒットマーク持続 (frames, capped at 20) */

    /* 演出・手応えチューニング (#046) */
    uint32_t shake_pct;        /* 画面振動倍率 (%) */
    uint32_t cutin_frames;     /* ボス出現カットイン長 (frames) */
    uint32_t victory_frames;   /* ボス勝利演出長 (frames) */
};

extern Params g;

void print_all();
bool apply(const char* key, long value);

} // namespace gp
