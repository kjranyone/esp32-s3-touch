/**
 * beat.h — オートバトルエンジン (v3.1)
 * 『ルミの水晶庭園』: 見て楽しむ、放置系オートバトラー
 *
 * - 味方 3 ユニット (LUMI/MIRA/GORO) が AI で自動戦闘
 * - 攻撃モーション: 構え(WINDUP)→攻撃(STRIKE)→フォロースルー(RECOVER) (#モーション)
 * - 必殺技: レイジゲージ満タンで条件発動。カットイン+明暗フラッシュ+ヒットストップ+大画面振動
 * - 弾 (遠距離攻撃) / ダメージ数字 / ヒットマーク / ウェーブ管理
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace beat {

constexpr int FB_W = 480;
constexpr int FB_H = 352;
constexpr float BELT_Y0 = 120.0f;
constexpr float BELT_Y1 = 320.0f;

constexpr int ALLY_N = 3;   /* LUMI, MIRA, GORO */
constexpr int ENEMY_N = 8;

void init();
void step();
void render(uint16_t* fb);
void refresh_player();      /* スキル購入後に味方 HP 等を再計算 */
void set_wave(uint32_t w);
uint32_t wave();
void heal_all();

/* HUD 用 */
float ally_hp(int i);
float ally_hpmax(int i);
float ally_rage(int i);     /* 必殺技ゲージ 0..100 */

/* 画面内メッセージ (内蔵ミニフォントで fb に直接描画) */
void flash_message(const char* msg, uint16_t frames);

/* LVGL がゲーム領域を壊した (タッチ等) ときに呼ぶ: 次フレームで背景全体を復元 */
void request_full_restore();

/* デバッグ: 全アクタの座標を出力 */
void dump();

} // namespace beat
