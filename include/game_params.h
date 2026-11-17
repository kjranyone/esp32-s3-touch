/**
 * game_params.h — 調整パラメータの外部定義
 *
 * 桜井スキル #046「調整をしやすくする仕組み」に基づき、
 * 調整したい数値はプログラムに直書きせずここに集約する。
 * シリアルコンソール (115200) から `キー 値` で実行中に書き換えられる。
 *
 * 使い方例:
 *   seed_grow 100     → 種の成長確率を 100/1000/フレーム に
 *   ?                 → 現在値をすべて表示
 *   reset             → 庭園を初期化
 */
#pragma once
#include <stdint.h>

namespace gp {

struct Params {
    /* 物理エンジン */
    uint32_t seed_grow;        /* 種の成長確率 (x/1000 per frame) */
    uint32_t crystal_grow;     /* 結晶の伝播確率 (x/1000 per frame) */
    uint32_t up_bias;          /* 成長が上方向を優先する度合い (%) */

    /* エミッタ (Phase 4 で施設に接続) */
    uint32_t emit_water;       /* 水滴の間隔 (フレーム) */
    uint32_t emit_sand;        /* 鉱砂の間隔 (フレーム) */
    uint32_t ambient_seed;     /* 自然に落ちる種の間隔 (フレーム) */

    /* 操作 */
    uint32_t brush;            /* スタンプ半径 (セル) */
};

extern Params g;   /* 実行中に書き換え可能なグローバルパラメータ */

void print_all();                          /* 現在値をシリアルへダンプ */
bool apply(const char* key, long value);   /* キー名で値を更新 (成功で true) */

} // namespace gp
