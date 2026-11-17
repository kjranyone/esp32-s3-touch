/**
 * sandbox.h — 落下砂モデル (falling-sand cellular automaton) 物理エンジン
 * 『ルミの水晶庭園』Phase 1
 *
 * - 論理グリッド 240x172、1 セルを 2x2 px で 480x344 に描画
 * - 素材: 空 / 岩 / 砂 / 水 / 結晶の種 / 結晶
 * - 水が種・結晶に触れると対角成長し、結晶セルが増える (将来: 収入に直結)
 */
#pragma once
#include <stdint.h>

namespace sandbox {

enum Mat : uint8_t {
    EMPTY = 0,
    ROCK,
    SAND,
    WATER,
    SEED,
    CRYSTAL,
    MAT_COUNT
};

/* 論理グリッドサイズ (表示は CELL 倍) */
constexpr int GW = 240;
constexpr int GH = 172;
constexpr int CELL = 2;              /* 1 セルあたりの表示ピクセル */
constexpr int FB_W = GW * CELL;      /* 480 */
constexpr int FB_H = GH * CELL;      /* 344 */

/* 初期化 (グリッド確保 + デモ地形の構築) */
void init();

/* 1 タイック分の物理更新 (30fps 想定) */
void step();

/* グリッドを RGB565 フレームバッファに描画 */
void render(uint16_t* fb);

/* グリッド座標 (gx, gy) に半径 radius で素材をスタンプ (EMPTY は消しゴム) */
void stamp(int gx, int gy, int radius, Mat m);

/* HUD 用の集計 */
int count(Mat m);

} // namespace sandbox
