/**
 * beat.h — ベルトスクロール戦闘エンジン (Phase 1')
 * 『ルミの水晶庭園』v3
 *
 * - 仮想パッド + ATK/DASH ボタン入力 (main.cpp から渡す)
 * - 3 段コンボ / ヒットストップ / ヒットフラッシュ / ノックバック / 画面振動
 * - ダメージ数字 (内蔵 3x5 ミニフォント) / 敵 AI 3 種 + ボス / ウェーブ管理
 * - 描画は 480x352 RGB565 フレームバッファへ直接 (スプライトはプレースホルダ図形)
 */
#pragma once
#include <stdint.h>

namespace beat {

constexpr int FB_W = 480;
constexpr int FB_H = 352;
constexpr float WORLD_W = 2400.0f;
constexpr float BELT_Y0 = 120.0f;   /* 歩行帯 (canvas 座標) */
constexpr float BELT_Y1 = 320.0f;

struct Pad {
    float dx, dy;        /* -1..1 (仮想パッド) */
    bool atk;            /* 1 回押し (main が立てて step が消す) */
    bool dash;
};
extern Pad pad;

void init();
void step();
void render(uint16_t* fb);
void refresh_player();       /* スキル購入後に HP 上限などを再計算 */
void set_wave(uint32_t w);
uint32_t wave();
float player_hp();           /* HUD 表示用 */
float player_hpmax();
void heal_player();          /* シリアルデバッグ用 */

} // namespace beat
