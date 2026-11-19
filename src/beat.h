/**
 * beat.h — オートバトルエンジン (v4.0 ローグライト仕様)
 * 『ルミの水晶庭園』: 全50ウェーブ Finite ローグライト / 64キャラ / 36チャーム / 6ボス
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace beat {

constexpr int FB_W = 480;
constexpr int FB_H = 352;
constexpr float BELT_Y0 = 120.0f;
constexpr float BELT_Y1 = 320.0f;

constexpr int ALLY_N = 3;
constexpr int ENEMY_N = 8;

void init();
void step();
void render(uint16_t* fb, int buf);
void render_stats(int64_t* restore_us, int64_t* draw_us);
void sync_party(bool reset_pos = false);
void set_wave(uint32_t w);
uint32_t wave();
void heal_all();

/* HUD 用 */
bool  ally_active(int i);
float ally_hp(int i);
float ally_hpmax(int i);
float ally_rage(int i);

/* 画面内メッセージ */
void flash_message(const char* msg, uint16_t frames);

/* インタラクション・演出トリガ */
bool is_title();
void enter_title();
void force_opening();
bool is_modal_director();
bool notify_tap(int x, int y);
void on_pet();
void on_draft_pick(int pick_result);
void force_boss();
void force_boss_phase(int pct);
void force_ult();
void force_win();
void force_gameover();
void force_ending();

void request_full_restore();
void dump();

/* ---------- UI レイアウト定数 (fb 直接描画 / docs/UX_GUIDELINE.md 準拠) ---------- */
constexpr int PANEL_X = 0, PANEL_Y = 56, PANEL_W = 480, PANEL_H = 352;
constexpr int CLOSE_X = 412, CLOSE_Y = 58, CLOSE_W = 60, CLOSE_H = 44;
constexpr int PEEKBTN_X = 276, PEEKBTN_Y = 58, PEEKBTN_W = 128, PEEKBTN_H = 44;

/* BOX ボタン (HUD 右下) */
constexpr int BOXBTN_X = 392, BOXBTN_Y = 410, BOXBTN_W = 82, BOXBTN_H = 66;

/* 3択ドラフトカード / 入れ替えカード矩形 */
constexpr int CARD_W = 146, CARD_H = 292, CARD_Y = 108;
constexpr int CARD_X[3] = {12, 167, 322};
constexpr int SWAP_W = 146, SWAP_H = 212, SWAP_Y = 186;
constexpr int SWAP_X[3] = {12, 167, 322};

void render_hud(uint16_t* fb, int buf);
void render_panel(uint16_t* fb, bool panel_open, bool info_peek, int buf);

} // namespace beat
