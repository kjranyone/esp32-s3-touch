/**
 * game_params.h — 調整パラメータの外部定義 (#046)
 *
 * シリアルコンソール (115200) から `キー 値` で実行中に書き換えられる。
 *   dmg_pct 125     → ダメージ倍率 125%
 *   ?               → 現在値をすべて表示
 *   reset           → 庭園(ステージ)を初期化
 *
 * スキル購入はこの値を書き換えることで「即時反映」される。
 */
#pragma once
#include <stdint.h>

namespace gp {

struct Params {
    /* 戦闘 (スキルで強化される基礎値) */
    uint32_t dmg_pct;          /* ダメージ倍率 (%) */
    uint32_t move_pct;         /* 移動速度倍率 (%) */
    uint32_t reach_pct;        /* 攻撃射程倍率 (%) */
    uint32_t hp_pct;           /* 最大HP倍率 (%) */

    /* 放置収入 */
    uint32_t idle_x10;         /* 放置収入 x10 (5 = 0.5/s) */

    /* なでなでバフ */
    uint32_t pet_buff_pct;     /* バフ率 (%) */
    uint32_t pet_duration_min; /* バフ持続 (分) */
};

extern Params g;

void print_all();
bool apply(const char* key, long value);

} // namespace gp
