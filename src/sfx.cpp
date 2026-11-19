/**
 * sfx.cpp — マルチエンジン DSP 音声合成エンジン + ESP32-S3 NS4168 I2S ドライバ
 *
 * 簡易な矩形波ビープではなく、以下の 4 系統の本格的なリアルタイム音響合成を搭載:
 *   1. 2-Op FM 合成 (高調波/非整数次キャリア・モジュレータ + モジュレーション指数エンベロープ)
 *      -> 水晶チャイム、ベル、ハープ、金属斬撃、ファンファーレ
 *   2. 3-Mode モーダル物理モデル (減衰正弦波バンク + インパルス励起)
 *      -> 結晶共鳴、木琴/石畳の打音、宝箱ラッチ、アーマー打撃
 *   3. PolyBLEP アンチエイリアス減算合成 + Chamberlin SVF (LP/BP/HP) + カットオフエンベロープ
 *      -> 重厚なブラス警告、サブベース衝撃、ウォームパッド
 *   4. SVF フィルタ付きホワイト/ピンクノイズ + トランジェントシェイパ
 *      -> 打撃インパクト、風切り、爆発、足音グラベル
 *
 * 追加 DSP ステージ:
 *   - 指数減衰 ADSR エンベロープ (クリックレス)
 *   - 連続ヒット時の自然な微細ピッチ/ベロシティ揺らぎ (マシンガン感排除)
 *   - 1次 DC ブロッカー + スムーズ・ソフトサチュレーションリミッタ
 *   - ESP32-S3 実機のみ軽量アーリーリフレクション内蔵 (Web 版はドライ出力し、
 *     ブラウザ側 Web Audio API の実 IR 畳み込み + HRTF 3D 空間音響に委ねる)
 */
#include "sfx.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

#if defined(ESP32) && !defined(__EMSCRIPTEN__)
  #ifndef SFX_ENABLE_I2S
    #define SFX_ENABLE_I2S 1
  #endif
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <driver/i2s_std.h>
  #include <driver/gpio.h>
#else
  #define SFX_ENABLE_I2S 0
#endif

namespace sfx {

/* ---------------- 高速 1024-pt サイン LUT (線形補間) ---------------- */
static constexpr int SIN_LUT_N = 1024;
static float sin_lut[SIN_LUT_N + 1];
static bool  lut_ready = false;

static void init_sin_lut()
{
    if (lut_ready) return;
    constexpr float TAU_F = 6.28318530717958647692f;
    for (int i = 0; i <= SIN_LUT_N; ++i) {
        sin_lut[i] = sinf(TAU_F * (float)i / (float)SIN_LUT_N);
    }
    lut_ready = true;
}

static inline float fast_sin(float phase01)
{
    float p = phase01 - floorf(phase01);
    float idx = p * (float)SIN_LUT_N;
    int i0 = (int)idx;
    float frac = idx - (float)i0;
    return sin_lut[i0] + (sin_lut[i0 + 1] - sin_lut[i0]) * frac;
}

/* PolyBLEP 残差 (エイリアシング抑圧用) */
static inline float poly_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* ---------------- 合成エンジン種別 & ノート定義 ---------------- */
enum SynthType : uint8_t {
    ST_FM = 0,       /* 2-Op FM (p1=mod_ratio, p2=mod_idx0, p3=mod_idx1) */
    ST_MODAL,        /* 3-Mode 物理共振 (p1=ratio2, p2=ratio3, p3=strike_click) */
    ST_SUB_SAW,      /* PolyBLEP Saw + SVF (p1=cut0_hz, p2=cut1_hz, p3=res_q) */
    ST_SUB_PULSE,    /* PolyBLEP Pulse + SVF (p1=cut0_hz, p2=cut1_hz, p3=pw) */
    ST_BODY_SINE,    /* ピッチドロップ胴鳴り + 軽微倍音 (p1=drive, p2=sub_mix, p3=curve) */
    ST_NOISE_LP,     /* SVF LP ノイズ (p1=cut0_hz, p2=cut1_hz, p3=res_q) */
    ST_NOISE_BP,     /* SVF BP ノイズ (p1=cut0_hz, p2=cut1_hz, p3=res_q) */
    ST_NOISE_HP,     /* SVF HP ノイズ (p1=cut0_hz, p2=cut1_hz, p3=res_q) */
};

struct LayerSpec {
    uint16_t  delay_ms;  /* 発音開始オフセット */
    uint16_t  dur_ms;    /* 持続時間 */
    float     f0;        /* 開始周波数 Hz */
    float     f1;        /* 終了周波数 Hz (指数スイープ) */
    SynthType type;
    uint8_t   vol;       /* 0..100 */
    uint8_t   atk_ms;    /* アタック ms */
    float     decay_k;   /* 指数減衰係数 (大きいほど鋭く減衰: 2.0=余韻長め, 7.0=パーカッシブ) */
    float     p1;        /* パラメータ1 */
    float     p2;        /* パラメータ2 */
    float     p3;        /* パラメータ3 */
};

struct Preset {
    const char*      label;
    uint16_t         cooldown_ms;
    uint8_t          priority;
    float            jitter_pct; /* 自然なピッチ揺らぎ % */
    uint8_t          count;
    const LayerSpec* layers;
};

/* ============================================================================
 * 全 24 種のサウンドデザイン (FM 水晶・モーダル物理打音・SVF 減算・ノイズ層)
 * ============================================================================ */

/* UI: タップ — 繊細なガラス＆木製マイクロクリック */
static const LayerSpec L_TAP[] = {
    {   0,  42, 1320.0f, 1320.0f, ST_MODAL,     40,  1, 8.5f, 2.756f, 4.10f, 0.35f },
    {   0,  18, 4200.0f, 1800.0f, ST_NOISE_BP,  20,  1, 9.0f, 3800.0f, 2200.0f, 2.2f },
};

/* UI: 決定 — 透明な水晶 FM ベル 5度上昇 (E5 -> B5) */
static const LayerSpec L_CONFIRM[] = {
    {   0, 150,  659.25f,  659.25f, ST_FM,      44,  2, 4.8f, 2.00f, 2.4f, 0.15f },
    {  46, 230,  987.77f,  987.77f, ST_FM,      48,  2, 4.2f, 2.756f, 1.9f, 0.10f },
    {  46, 170, 1975.53f, 1975.53f, ST_MODAL,   20,  2, 6.0f, 2.756f, 5.40f, 0.10f },
};

/* UI: キャンセル — 柔らかいマリンバ 2音下降 */
static const LayerSpec L_CANCEL[] = {
    {   0, 100,  587.33f,  582.0f,  ST_MODAL,   44,  2, 6.2f, 3.98f, 8.20f, 0.25f },
    {  52, 150,  440.00f,  432.0f,  ST_MODAL,   42,  2, 5.5f, 3.98f, 8.20f, 0.20f },
};

/* UI: ポーズ — 硝子ハープの浮遊感ある和音 (C5 - G5 - D6) */
static const LayerSpec L_PAUSE[] = {
    {   0, 220,  523.25f,  523.25f, ST_FM,      38,  3, 4.5f, 2.00f, 1.8f, 0.10f },
    {  42, 240,  783.99f,  783.99f, ST_FM,      40,  3, 4.2f, 2.00f, 1.6f, 0.10f },
    {  84, 300, 1174.66f, 1174.66f, ST_FM,      38,  4, 3.8f, 3.00f, 1.4f, 0.08f },
};

/* 戦闘: 軽ヒット — トランジェント + 胴鳴り + 空気斬撃 */
static const LayerSpec L_HIT_LIGHT[] = {
    {   0,  75,  210.0f,   56.0f,   ST_BODY_SINE, 66, 1, 6.5f, 1.4f, 0.30f, 1.0f },
    {   0,  42, 3200.0f,  900.0f,   ST_NOISE_BP,  44, 1, 7.5f, 2600.0f, 850.0f, 1.6f },
    {   0,  22, 6500.0f, 2500.0f,   ST_NOISE_HP,  30, 1, 9.5f, 4500.0f, 2400.0f, 1.2f },
};

/* 戦闘: 重ヒット — 重低音スラム + 金属共鳴 + 厚い衝撃ノイズ */
static const LayerSpec L_HIT_HEAVY[] = {
    {   0, 165,  155.0f,   36.0f,   ST_BODY_SINE, 80, 1, 4.8f, 2.1f, 0.55f, 1.0f },
    {   0, 110, 1900.0f,  260.0f,   ST_NOISE_BP,  56, 1, 5.5f, 1400.0f, 280.0f, 1.8f },
    {   4, 135,  320.0f,  195.0f,   ST_FM,        34, 2, 5.8f, 2.756f, 3.6f, 0.4f },
    {   0,  34, 5200.0f, 1400.0f,   ST_NOISE_HP,  36, 1, 8.5f, 3600.0f, 1600.0f, 1.1f },
};

/* 戦闘: クリティカル — 衝撃スラム + 閃光クリスタル FM ハーモニック */
static const LayerSpec L_HIT_CRIT[] = {
    {   0, 120,  190.0f,   44.0f,   ST_BODY_SINE, 72, 1, 5.6f, 1.8f, 0.40f, 1.0f },
    {   0,  55, 4200.0f, 1200.0f,   ST_NOISE_BP,  46, 1, 6.8f, 3400.0f, 1100.0f, 2.0f },
    {   0, 220, 1318.51f, 1318.51f, ST_FM,        48, 1, 4.5f, 2.756f, 3.4f, 0.15f },
    {  28, 260, 1975.53f, 1975.53f, ST_MODAL,     38, 1, 4.4f, 2.756f, 5.40f, 0.20f },
};

/* 戦闘: 遠距離魔法/矢 — 風切り BP ノイズ + 軽快な結晶スパーク */
static const LayerSpec L_PROJ_SHOOT[] = {
    {   0,  85, 2200.0f,  750.0f,   ST_NOISE_BP,  30, 2, 6.2f, 2600.0f, 900.0f, 2.6f },
    {   0,  95, 1120.0f,  580.0f,   ST_FM,        34, 2, 6.8f, 2.00f, 1.9f, 0.10f },
};

/* 戦闘: 味方被弾 — 重めの鎧打撃 + 低域ノイズ */
static const LayerSpec L_HURT_ALLY[] = {
    {   0, 125,  165.0f,   52.0f,   ST_BODY_SINE, 62, 1, 5.4f, 1.9f, 0.35f, 1.0f },
    {   0, 105,  220.0f,   88.0f,   ST_SUB_SAW,   38, 2, 5.8f,  950.0f, 240.0f, 2.2f },
    {   0,  68, 1200.0f,  340.0f,   ST_NOISE_LP,  38, 1, 6.5f, 1100.0f, 320.0f, 1.3f },
};

/* 戦闘: 敵撃破 — 弾ける破裂 + 飛び散る結晶チャイム */
static const LayerSpec L_ENEMY_KO[] = {
    {   0, 105,  140.0f,   42.0f,   ST_BODY_SINE, 52, 1, 5.8f, 1.5f, 0.25f, 1.0f },
    {   0, 115, 2400.0f,  320.0f,   ST_NOISE_LP,  44, 1, 5.5f, 2200.0f, 350.0f, 1.8f },
    {  22, 195, 1046.50f, 1046.50f, ST_MODAL,     36, 2, 5.0f, 2.756f, 5.40f, 0.15f },
    {  54, 210, 1567.98f, 1567.98f, ST_FM,        32, 2, 4.8f, 2.00f, 1.5f, 0.08f },
};

/* 報酬: クリスタル獲得 — 澄んだ水晶 3-Mode 共振アルペジオ */
static const LayerSpec L_CRYSTAL[] = {
    {   0, 210, 1318.51f, 1318.51f, ST_MODAL,     40, 1, 4.8f, 2.756f, 5.404f, 0.18f },
    {  48, 260, 1975.53f, 1975.53f, ST_MODAL,     44, 1, 4.4f, 2.756f, 5.404f, 0.18f },
    {  96, 320, 2637.02f, 2637.02f, ST_FM,        34, 2, 4.2f, 2.00f,  1.2f,   0.05f },
};

/* 必殺技発動 — SVF スイープ上昇 + FM パワー和音 + 衝撃波 */
static const LayerSpec L_ULT_CAST[] = {
    {   0, 220,  261.63f,  523.25f, ST_SUB_SAW,   44, 12, 3.2f,  600.0f, 3800.0f, 3.2f },
    {  40, 260,  523.25f, 1046.50f, ST_FM,        46, 10, 3.4f,  2.00f,  3.2f,    0.3f },
    { 130, 380,  783.99f,  783.99f, ST_FM,        48,  5, 3.0f,  1.00f,  2.4f,    0.2f },
    { 140, 420, 1046.50f, 1046.50f, ST_MODAL,     42,  4, 3.0f,  2.756f, 5.40f,   0.2f },
    { 125, 220,  130.0f,    38.0f,  ST_BODY_SINE, 62,  2, 4.2f,  1.8f,   0.45f,   1.0f },
    { 125, 210, 2800.0f,   450.0f,  ST_NOISE_BP,  40,  4, 4.2f, 2400.0f,  520.0f, 1.5f },
};

/* なでなで — 温かいチェレスタ/マリンバの愛らしい上昇トライアド */
static const LayerSpec L_PET[] = {
    {   0, 170,  880.00f,  880.00f, ST_MODAL,     40,  2, 5.2f, 3.98f, 8.20f, 0.22f },
    {  58, 190, 1108.73f, 1108.73f, ST_MODAL,     42,  2, 5.0f, 3.98f, 8.20f, 0.22f },
    { 116, 280, 1318.51f, 1318.51f, ST_FM,        44,  3, 4.0f, 2.00f, 1.5f,  0.08f },
    { 116, 260, 1760.00f, 1760.00f, ST_MODAL,     26,  4, 4.5f, 2.756f, 5.40f, 0.10f },
};

/* ルートボックス開封 — 留め金クリック + 煌めく FM Maj9 展開 */
static const LayerSpec L_BOX_OPEN[] = {
    {   0,  28, 2200.0f,  900.0f,   ST_NOISE_BP,  34,  1, 8.5f, 2400.0f, 1100.0f, 2.8f },
    {  20, 180,  523.25f,  523.25f, ST_FM,        42,  2, 4.5f, 2.00f, 2.2f, 0.15f },
    {  72, 190,  659.25f,  659.25f, ST_FM,        44,  2, 4.4f, 2.00f, 2.0f, 0.15f },
    { 124, 210,  783.99f,  783.99f, ST_FM,        46,  2, 4.2f, 2.00f, 2.0f, 0.12f },
    { 176, 390, 1046.50f, 1046.50f, ST_FM,        50,  3, 3.2f, 2.756f, 2.0f, 0.10f },
    { 186, 400, 1174.66f, 1174.66f, ST_MODAL,     34,  4, 3.4f, 2.756f, 5.40f, 0.10f },
};

/* ドラフト決定 (仲間加入/ランクアップ/チャーム装備) — 華麗なハープ＆ベル */
static const LayerSpec L_DRAFT_PICK[] = {
    {   0, 180,  587.33f,  587.33f, ST_FM,        44,  2, 4.6f, 1.00f, 2.6f, 0.20f },
    {  60, 190,  739.99f,  739.99f, ST_FM,        46,  2, 4.5f, 1.00f, 2.5f, 0.20f },
    { 120, 210,  880.00f,  880.00f, ST_FM,        48,  2, 4.2f, 2.00f, 2.2f, 0.15f },
    { 180, 380, 1174.66f, 1174.66f, ST_FM,        52,  3, 3.2f, 2.00f, 2.4f, 0.12f },
    { 180, 380,  587.33f,  587.33f, ST_SUB_SAW,   26,  6, 3.4f, 2400.0f, 800.0f, 1.2f },
};

/* 行軍足音 — 石畳の柔らかい靴音 + 微細な砂利ノイズ */
static const LayerSpec L_STEP[] = {
    {   0,  46,  145.0f,   62.0f,   ST_BODY_SINE, 34,  1, 8.2f, 1.2f, 0.20f, 1.0f },
    {   0,  34, 1800.0f,  700.0f,   ST_NOISE_BP,  20,  2, 8.5f, 1500.0f, 650.0f, 1.8f },
};

/* キャンプ休憩 — 暖炉のそばの温かいアコースティックハープ分散和音 */
static const LayerSpec L_CAMP_REST[] = {
    {   0, 280,  392.00f,  392.00f, ST_FM,        44,  4, 3.6f, 1.00f, 1.8f, 0.12f },
    {  80, 280,  493.88f,  493.88f, ST_FM,        44,  4, 3.6f, 1.00f, 1.7f, 0.12f },
    { 160, 300,  587.33f,  587.33f, ST_FM,        46,  4, 3.4f, 1.00f, 1.6f, 0.10f },
    { 240, 440,  783.99f,  783.99f, ST_FM,        48,  5, 2.8f, 2.00f, 1.5f, 0.08f },
    { 240, 400,  392.00f,  392.00f, ST_BODY_SINE, 30,  8, 3.0f, 1.0f,  0.20f, 1.0f },
};

/* 宝箱開封 — 鍵解錠クリック + オーケストラ風 5音ファンファーレ */
static const LayerSpec L_CHEST_OPEN[] = {
    {   0,  35, 1600.0f,  600.0f,   ST_NOISE_BP,  36,  1, 8.0f, 1800.0f, 700.0f, 2.4f },
    {  35, 190,  587.33f,  587.33f, ST_FM,        46,  2, 4.4f, 2.00f, 2.4f, 0.2f },
    { 110, 190,  739.99f,  739.99f, ST_FM,        48,  2, 4.4f, 2.00f, 2.3f, 0.2f },
    { 185, 190,  880.00f,  880.00f, ST_FM,        50,  2, 4.2f, 2.00f, 2.2f, 0.18f },
    { 260, 200, 1174.66f, 1174.66f, ST_FM,        52,  2, 4.0f, 2.00f, 2.2f, 0.15f },
    { 345, 520, 1479.98f, 1479.98f, ST_FM,        54,  4, 2.6f, 2.756f, 2.4f, 0.12f },
    { 345, 500,  587.33f,  587.33f, ST_SUB_SAW,   32, 10, 2.8f, 2600.0f, 900.0f, 1.4f },
};

/* 神秘イベント — リディアン旋法の幻想的なガラスベル残響 */
static const LayerSpec L_EVENT_GOOD[] = {
    {   0, 260,  523.25f,  523.25f, ST_MODAL,     42,  4, 3.6f, 2.756f, 5.40f, 0.12f },
    {  72, 290,  739.99f,  739.99f, ST_FM,        42,  5, 3.4f, 2.00f,  1.9f,  0.10f },
    { 148, 380,  987.77f,  987.77f, ST_MODAL,     44,  5, 3.0f, 2.756f, 5.40f, 0.12f },
    { 225, 420, 1479.98f, 1479.98f, ST_FM,        36,  6, 2.8f, 3.00f,  1.4f,  0.06f },
};

/* ウェーブクリア — 爽快なブラス＆クリスタル 4音勝利ジングル */
static const LayerSpec L_WAVE_CLEAR[] = {
    {   0, 160,  523.25f,  523.25f, ST_FM,        46,  2, 4.5f, 1.00f, 2.5f, 0.25f },
    {  78, 160,  659.25f,  659.25f, ST_FM,        48,  2, 4.5f, 1.00f, 2.4f, 0.25f },
    { 156, 170,  783.99f,  783.99f, ST_FM,        50,  2, 4.2f, 2.00f, 2.2f, 0.20f },
    { 234, 420, 1046.50f, 1046.50f, ST_FM,        54,  3, 2.9f, 2.00f, 2.4f, 0.15f },
    { 234, 400,  523.25f,  523.25f, ST_SUB_SAW,   32,  8, 3.2f, 2400.0f, 750.0f, 1.5f },
};

/* ボス出現警告 — シネマティックな低音ブラスホーン + 戦太鼓サブ打撃 */
static const LayerSpec L_BOSS_WARN[] = {
    {   0, 310,  110.00f,   98.00f, ST_SUB_SAW,   58, 18, 2.6f, 1400.0f, 280.0f, 2.4f },
    {   0, 310,  110.80f,   98.70f, ST_SUB_SAW,   48, 20, 2.6f, 1200.0f, 260.0f, 2.0f },
    {   0, 190,   92.0f,    36.0f,  ST_BODY_SINE, 70,  2, 4.2f, 1.8f,    0.55f,  1.0f },
    { 290, 390,   98.00f,   82.41f, ST_SUB_SAW,   62, 16, 2.2f, 1600.0f, 240.0f, 2.6f },
    { 290, 230,   88.0f,    34.0f,  ST_BODY_SINE, 72,  2, 3.8f, 2.0f,    0.60f,  1.0f },
    { 290, 220,  900.0f,   180.0f,  ST_NOISE_LP,  34,  6, 4.2f,  850.0f, 200.0f, 1.5f },
};

/* ボス撃破 — 荘厳なマルチレイヤー勝利ファンファーレ */
static const LayerSpec L_BOSS_WIN[] = {
    {   0, 160,  523.25f,  523.25f, ST_FM,        48,  2, 4.4f, 1.00f, 2.8f, 0.30f },
    {  82, 160,  659.25f,  659.25f, ST_FM,        50,  2, 4.4f, 1.00f, 2.7f, 0.30f },
    { 164, 160,  783.99f,  783.99f, ST_FM,        52,  2, 4.2f, 1.00f, 2.6f, 0.25f },
    { 246, 140, 1046.50f, 1046.50f, ST_FM,        54,  2, 4.4f, 2.00f, 2.4f, 0.20f },
    { 324, 140,  783.99f,  783.99f, ST_FM,        48,  2, 4.4f, 2.00f, 2.2f, 0.20f },
    { 404, 620, 1046.50f, 1046.50f, ST_FM,        58,  4, 2.2f, 2.00f, 2.6f, 0.15f },
    { 404, 600,  523.25f,  523.25f, ST_SUB_SAW,   38, 12, 2.4f, 2800.0f, 820.0f, 1.4f },
    { 412, 580, 1567.98f, 1567.98f, ST_MODAL,     36,  6, 2.4f, 2.756f, 5.40f, 0.15f },
};

/* ゲームオーバー — 切ないオルゴール / チェレスタのマイナー下降 */
static const LayerSpec L_GAME_OVER[] = {
    {   0, 290,  440.00f,  438.0f,  ST_MODAL,     46,  4, 3.4f, 2.756f, 5.40f, 0.15f },
    { 170, 290,  392.00f,  390.0f,  ST_MODAL,     44,  4, 3.4f, 2.756f, 5.40f, 0.15f },
    { 340, 310,  349.23f,  347.0f,  ST_FM,        44,  5, 3.2f, 1.00f,  1.5f,  0.10f },
    { 510, 620,  293.66f,  261.63f, ST_FM,        46,  8, 2.3f, 1.00f,  1.4f,  0.08f },
    { 510, 560,  130.81f,  130.81f, ST_BODY_SINE, 34, 15, 2.4f, 1.0f,   0.15f, 1.0f },
};

/* エンディング (Wave 50 完全制覇) — 輝くクリスタルハープ＆グランドフィナーレ */
static const LayerSpec L_ENDING[] = {
    {   0, 220,  523.25f,  523.25f, ST_FM,        46,  3, 3.8f, 2.00f, 2.0f, 0.15f },
    {  92, 220,  659.25f,  659.25f, ST_FM,        48,  3, 3.8f, 2.00f, 2.0f, 0.15f },
    { 184, 220,  783.99f,  783.99f, ST_FM,        50,  3, 3.6f, 2.00f, 2.0f, 0.15f },
    { 276, 260, 1046.50f, 1046.50f, ST_MODAL,     48,  3, 3.4f, 2.756f, 5.40f, 0.15f },
    { 380, 240,  987.77f,  987.77f, ST_FM,        48,  3, 3.6f, 2.00f, 1.8f, 0.12f },
    { 474, 260, 1174.66f, 1174.66f, ST_FM,        50,  3, 3.4f, 2.00f, 1.8f, 0.12f },
    { 575, 760, 1567.98f, 1567.98f, ST_FM,        54,  6, 2.0f, 2.00f, 2.2f, 0.10f },
    { 575, 760,  523.25f,  523.25f, ST_SUB_SAW,   38, 16, 2.0f, 2600.0f, 900.0f, 1.3f },
    { 590, 740, 1046.50f, 1046.50f, ST_MODAL,     40,  6, 2.1f, 2.756f, 5.40f, 0.12f },
};

#define MK_PRESET(lbl, cd, prio, jit, arr) { lbl, cd, prio, jit, (uint8_t)(sizeof(arr) / sizeof((arr)[0])), arr }

static const Preset PRESETS[SFX_COUNT] = {
    MK_PRESET("tap",        30, 1, 1.5f, L_TAP),
    MK_PRESET("confirm",    45, 2, 0.0f, L_CONFIRM),
    MK_PRESET("cancel",     45, 2, 0.0f, L_CANCEL),
    MK_PRESET("pause",      70, 3, 0.0f, L_PAUSE),
    MK_PRESET("hit_light",  42, 1, 3.8f, L_HIT_LIGHT),
    MK_PRESET("hit_heavy",  55, 2, 2.5f, L_HIT_HEAVY),
    MK_PRESET("hit_crit",   50, 3, 1.8f, L_HIT_CRIT),
    MK_PRESET("shoot",      68, 1, 4.2f, L_PROJ_SHOOT),
    MK_PRESET("hurt",      110, 2, 2.5f, L_HURT_ALLY),
    MK_PRESET("ko",         65, 2, 3.0f, L_ENEMY_KO),
    MK_PRESET("crystal",    55, 2, 1.5f, L_CRYSTAL),
    MK_PRESET("ult",       180, 4, 0.0f, L_ULT_CAST),
    MK_PRESET("pet",       120, 3, 1.2f, L_PET),
    MK_PRESET("box_open",  120, 3, 0.0f, L_BOX_OPEN),
    MK_PRESET("draft_pick",120, 3, 0.0f, L_DRAFT_PICK),
    MK_PRESET("step",      125, 0, 5.0f, L_STEP),
    MK_PRESET("camp",      160, 3, 0.0f, L_CAMP_REST),
    MK_PRESET("chest",     160, 4, 0.0f, L_CHEST_OPEN),
    MK_PRESET("event",     130, 3, 0.0f, L_EVENT_GOOD),
    MK_PRESET("wave_clear",200, 4, 0.0f, L_WAVE_CLEAR),
    MK_PRESET("boss_warn", 260, 4, 0.0f, L_BOSS_WARN),
    MK_PRESET("boss_win",  260, 4, 0.0f, L_BOSS_WIN),
    MK_PRESET("gameover",  320, 4, 0.0f, L_GAME_OVER),
    MK_PRESET("ending",    320, 4, 0.0f, L_ENDING),
};

/* ---------------- 12 ボイス DSP ポリフォニック状態 ---------------- */
static constexpr int MAX_VOICES = 12;

struct Voice {
    bool      active;
    SynthType type;
    uint8_t   priority;
    uint32_t  delay_left;
    uint32_t  pos;
    uint32_t  total;
    uint32_t  atk_s;
    float     f0;
    float     f1;
    float     decay_k;
    float     gain;
    float     p1, p2, p3;
    float     ph0, ph1, ph2;  /* オシレータ / モジュレータ / 3次モード位相 */
    float     svf_lp, svf_bp; /* Chamberlin State-Variable Filter 状態 */
    uint32_t  rng;            /* 高品質 xorshift32 ノイズ生成 */
};

static Voice    voices[MAX_VOICES];
static uint32_t last_play_ms[SFX_COUNT];
static uint32_t s_rate = 22050;
static volatile bool    s_muted = false;
static volatile uint8_t s_vol_pct = 82;
static bool     s_inited = false;
static uint32_t s_jitter_rng = 0x9E3779B9u;

/* DC ブロッカー状態 */
static float dc_x1 = 0.0f;
static float dc_y1 = 0.0f;

#if SFX_ENABLE_I2S
/* ESP32-S3 実機スピーカー向け軽量アーリーリフレクション (Web版はIR畳み込みに委ねるため未使用) */
static constexpr int ER_LEN = 1024;
static float er_buf[ER_LEN];
static int   er_idx = 0;
#endif

#if defined(ESP32) && !defined(__EMSCRIPTEN__)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define SFX_LOCK()   portENTER_CRITICAL(&s_mux)
#define SFX_UNLOCK() portEXIT_CRITICAL(&s_mux)
#else
#define SFX_LOCK()   ((void)0)
#define SFX_UNLOCK() ((void)0)
#endif

static inline float next_white(uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)((int32_t)s) * 4.65661287e-10f; /* [-1, 1] */
}

/* ---------------- ESP32-S3 NS4168 I2S ドライバ ---------------- */
#if SFX_ENABLE_I2S
static i2s_chan_handle_t s_i2s_tx = nullptr;
static bool s_i2s_enabled = false;
static TaskHandle_t s_audio_task = nullptr;

static void i2s_apply_power(bool want_on)
{
    if (!s_i2s_tx) return;
    if (want_on && !s_i2s_enabled) {
        if (i2s_channel_enable(s_i2s_tx) == ESP_OK) s_i2s_enabled = true;
    } else if (!want_on && s_i2s_enabled) {
        i2s_channel_disable(s_i2s_tx);
        s_i2s_enabled = false;
    }
}

static void audio_task_fn(void*)
{
    constexpr int CHUNK = 256;
    static int16_t mono[CHUNK];
    static int16_t stereo[CHUNK * 2];

    while (true) {
        if (s_muted || !s_i2s_tx) {
            i2s_apply_power(false);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        i2s_apply_power(true);
        render_i16(mono, CHUNK);
        for (int i = 0; i < CHUNK; ++i) {
            int16_t v = mono[i];
            stereo[i * 2]     = v;
            stereo[i * 2 + 1] = v;
        }
        size_t written = 0;
        i2s_channel_write(s_i2s_tx, stereo, sizeof(stereo), &written, pdMS_TO_TICKS(40));
    }
}

static void init_esp32_i2s()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, &s_i2s_tx, nullptr) != ESP_OK) {
        Serial.println("[sfx] i2s_new_channel failed");
        s_i2s_tx = nullptr;
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(s_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_1,
            .ws   = GPIO_NUM_2,
            .dout = GPIO_NUM_40,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    if (i2s_channel_init_std_mode(s_i2s_tx, &std_cfg) != ESP_OK) {
        Serial.println("[sfx] i2s_channel_init_std_mode failed");
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = nullptr;
        return;
    }
    i2s_apply_power(!s_muted);
    xTaskCreatePinnedToCore(audio_task_fn, "sfx_i2s", 3584, nullptr, 2, &s_audio_task, 0);
    Serial.printf("[sfx] DSP Synth + I2S NS4168 ready @ %uHz (BCLK=1 LRCK=2 DOUT=40)\r\n", (unsigned)s_rate);
}
#endif

void init()
{
    if (s_inited) return;
    s_inited = true;
    init_sin_lut();
    memset(voices, 0, sizeof(voices));
    memset(last_play_ms, 0, sizeof(last_play_ms));
    dc_x1 = dc_y1 = 0.0f;
#if SFX_ENABLE_I2S
    memset(er_buf, 0, sizeof(er_buf));
    er_idx = 0;
    init_esp32_i2s();
#endif
}

void set_sample_rate(uint32_t hz)
{
    if (hz < 8000) hz = 8000;
    if (hz > 96000) hz = 96000;
    SFX_LOCK();
    s_rate = hz;
    SFX_UNLOCK();
}

uint32_t sample_rate() { return s_rate; }

void set_muted(bool muted)
{
    s_muted = muted;
    if (muted) stop_all();
}

bool is_muted() { return s_muted; }

void set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_vol_pct = pct;
}

uint8_t volume() { return s_vol_pct; }

void stop_all()
{
    SFX_LOCK();
    for (int i = 0; i < MAX_VOICES; ++i) voices[i].active = false;
    SFX_UNLOCK();
}

int active_voices()
{
    int n = 0;
    SFX_LOCK();
    for (int i = 0; i < MAX_VOICES; ++i) if (voices[i].active) n++;
    SFX_UNLOCK();
    return n;
}

const char* name(Id id)
{
    if ((uint8_t)id >= SFX_COUNT) return "?";
    return PRESETS[id].label;
}

static int alloc_voice_locked(uint8_t prio)
{
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!voices[i].active) return i;
    }
    int best = -1;
    uint32_t best_score = 0xFFFFFFFFu;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices[i].priority > prio) continue;
        uint32_t rem = voices[i].delay_left + (voices[i].total - voices[i].pos);
        uint32_t score = ((uint32_t)voices[i].priority << 20) + (rem & 0xFFFFFu);
        if (score < best_score) { best_score = score; best = i; }
    }
    return best;
}

void play(Id id)
{
    if ((uint8_t)id >= SFX_COUNT || s_muted || s_vol_pct == 0) return;
    if (!lut_ready) init_sin_lut();

    const Preset& p = PRESETS[id];
    uint32_t now = (uint32_t)millis();
    if (p.cooldown_ms > 0 && last_play_ms[id] != 0 && (uint32_t)(now - last_play_ms[id]) < p.cooldown_ms) {
        return;
    }
    last_play_ms[id] = now ? now : 1;

    /* 自然なピッチ＆ベロシティ揺らぎ */
    float pitch_mul = 1.0f;
    float vel_mul = 1.0f;
    if (p.jitter_pct > 0.0f) {
        float r1 = next_white(s_jitter_rng);
        float r2 = next_white(s_jitter_rng);
        pitch_mul = 1.0f + r1 * (p.jitter_pct * 0.01f);
        vel_mul   = 1.0f + r2 * 0.07f;
    }

    SFX_LOCK();
    uint32_t sr = s_rate;
    for (uint8_t k = 0; k < p.count; ++k) {
        int slot = alloc_voice_locked(p.priority);
        if (slot < 0) break;
        const LayerSpec& L = p.layers[k];
        Voice& v = voices[slot];
        v.active     = true;
        v.type       = L.type;
        v.priority   = p.priority;
        v.delay_left = (uint32_t)(((uint64_t)L.delay_ms * sr) / 1000u);
        v.pos        = 0;
        v.total      = (uint32_t)(((uint64_t)L.dur_ms * sr) / 1000u);
        if (v.total < 16) v.total = 16;
        v.atk_s      = (uint32_t)(((uint64_t)L.atk_ms * sr) / 1000u);
        if (v.atk_s < 4) v.atk_s = 4;
        v.f0         = L.f0 * pitch_mul;
        v.f1         = L.f1 * pitch_mul;
        v.decay_k    = L.decay_k;
        v.gain       = (float)L.vol * 0.0046f * vel_mul;
        v.p1         = L.p1;
        v.p2         = L.p2;
        v.p3         = L.p3;
        v.ph0 = v.ph1 = v.ph2 = 0.0f;
        v.svf_lp = v.svf_bp = 0.0f;
        v.rng = 0x6D2B79F5u ^ ((uint32_t)(slot + 1) * 0x9E3779B9u) ^ (now * 2654435761u);
        if (v.rng == 0) v.rng = 0x12345678u;
    }
    SFX_UNLOCK();
}

static inline float soft_sat(float x)
{
    /* 高速・滑らかソフトサチュレーション (tanh 近似, [-1, 1]) */
    if (x > 2.2f) return 0.98f;
    if (x < -2.2f) return -0.98f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float synth_voice_sample(Voice& v, float inv_sr, float sr)
{
    if (v.delay_left > 0) {
        v.delay_left--;
        return 0.0f;
    }
    if (v.pos >= v.total) {
        v.active = false;
        return 0.0f;
    }

    float t = (float)v.pos / (float)v.total;

    /* 指数減衰 + クリックレス・アタック/テールフェード */
    float env = 1.0f;
    if (v.pos < v.atk_s) {
        float a = (float)v.pos / (float)v.atk_s;
        env = a * a;
    } else {
        float dt = (float)(v.pos - v.atk_s) / (float)(v.total - v.atk_s > 0 ? v.total - v.atk_s : 1);
        env = expf(-v.decay_k * dt);
        /* 終端 8% で滑らかにゼロへ収束させてクリック防止 */
        if (dt > 0.92f) env *= (1.0f - dt) * 12.5f;
    }

    /* 周波数スイープ (線形・指数ブレンド) */
    float freq = v.f0;
    if (v.f0 != v.f1) {
        /* 指数的ピッチエンベロープ (打撃・ドロップの自然さ) */
        float u = 1.0f - expf(-3.8f * t);
        freq = v.f0 + (v.f1 - v.f0) * u;
    }
    if (freq < 22.0f) freq = 22.0f;
    float nyq = sr * 0.44f;
    if (freq > nyq) freq = nyq;
    float dt_ph = freq * inv_sr;

    float out = 0.0f;

    switch (v.type) {
        case ST_FM: {
            /* 2-Op FM: モジュレーション指数が指数減衰し、澄んだ倍音から純音へ溶ける */
            float mod_env = expf(-(v.decay_k * 1.45f) * t);
            float mod_idx = v.p3 + (v.p2 - v.p3) * mod_env;
            v.ph1 += dt_ph * v.p1;
            if (v.ph1 >= 1.0f) v.ph1 -= floorf(v.ph1);
            float mod = fast_sin(v.ph1) * mod_idx * 0.15915494f; /* rad -> cycles */
            v.ph0 += dt_ph;
            if (v.ph0 >= 1.0f) v.ph0 -= 1.0f;
            out = fast_sin(v.ph0 + mod);
            break;
        }
        case ST_MODAL: {
            /* 3-Mode 物理共振バンク (基音 + 2倍音モード + 叩きインパルス / ナイキスト超え抑止) */
            v.ph0 += dt_ph;
            if (v.ph0 >= 1.0f) v.ph0 -= 1.0f;
            float d2 = expf(-v.decay_k * 1.9f * t);
            float d3 = expf(-v.decay_k * 3.1f * t);
            out = fast_sin(v.ph0) * 0.68f;
            if (freq * v.p1 < nyq) {
                v.ph1 += dt_ph * v.p1;
                if (v.ph1 >= 1.0f) v.ph1 -= floorf(v.ph1);
                out += fast_sin(v.ph1) * 0.28f * d2;
            }
            if (freq * v.p2 < nyq) {
                v.ph2 += dt_ph * v.p2;
                if (v.ph2 >= 1.0f) v.ph2 -= floorf(v.ph2);
                out += fast_sin(v.ph2) * 0.14f * d3;
            }
            /* 初期 3ms のマレット/打撃励起ノイズ */
            if (v.p3 > 0.0f && t < 0.06f) {
                float imp = (1.0f - t * 16.666f);
                out += next_white(v.rng) * v.p3 * imp * imp;
            }
            break;
        }
        case ST_BODY_SINE: {
            /* パンチのある胴鳴り (サイン + サブオクターブ + 軽微ソフトドライブ) */
            v.ph0 += dt_ph;
            v.ph1 += dt_ph * 0.5f;
            if (v.ph0 >= 1.0f) v.ph0 -= 1.0f;
            if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
            float s = fast_sin(v.ph0) + fast_sin(v.ph1) * v.p2;
            out = soft_sat(s * v.p1);
            break;
        }
        case ST_SUB_SAW:
        case ST_SUB_PULSE: {
            /* PolyBLEP アンチエイリアス波形 + Chamberlin SVF LP フィルタ */
            v.ph0 += dt_ph;
            if (v.ph0 >= 1.0f) v.ph0 -= 1.0f;
            float raw;
            if (v.type == ST_SUB_SAW) {
                raw = 2.0f * v.ph0 - 1.0f - poly_blep(v.ph0, dt_ph);
            } else {
                float pw = v.p3;
                raw = (v.ph0 < pw) ? 1.0f : -1.0f;
                raw += poly_blep(v.ph0, dt_ph);
                float ph_pw = v.ph0 - pw;
                if (ph_pw < 0.0f) ph_pw += 1.0f;
                raw -= poly_blep(ph_pw, dt_ph);
            }
            float cut = v.p1 + (v.p2 - v.p1) * t;
            if (cut < 60.0f) cut = 60.0f;
            if (cut > nyq * 0.9f) cut = nyq * 0.9f;
            float f = 2.0f * fast_sin(cut * inv_sr * 0.5f);
            if (f > 0.82f) f = 0.82f;
            float q = (v.type == ST_SUB_SAW) ? v.p3 : 1.4f;
            float damp = 1.0f / (q < 0.6f ? 0.6f : q);
            float hp = raw - v.svf_lp - damp * v.svf_bp;
            v.svf_bp += f * hp;
            v.svf_lp += f * v.svf_bp;
            out = v.svf_lp;
            break;
        }
        case ST_NOISE_LP:
        case ST_NOISE_BP:
        case ST_NOISE_HP: {
            float n = next_white(v.rng);
            float cut = v.p1 + (v.p2 - v.p1) * t;
            if (cut < 80.0f) cut = 80.0f;
            if (cut > nyq * 0.9f) cut = nyq * 0.9f;
            float f = 2.0f * fast_sin(cut * inv_sr * 0.5f);
            if (f > 0.82f) f = 0.82f;
            float q = v.p3 < 0.6f ? 0.6f : v.p3;
            float damp = 1.0f / q;
            float hp = n - v.svf_lp - damp * v.svf_bp;
            v.svf_bp += f * hp;
            v.svf_lp += f * v.svf_bp;
            if (v.type == ST_NOISE_LP)      out = v.svf_lp;
            else if (v.type == ST_NOISE_BP) out = v.svf_bp;
            else                            out = hp;
            break;
        }
    }

    v.pos++;
    if (v.pos >= v.total) v.active = false;
    return out * env * v.gain;
}

static inline float process_master_sample(float x)
{
    /* 1次 DC ブロッカー (R = 0.995) */
    float y = x - dc_x1 + 0.995f * dc_y1;
    dc_x1 = x;
    dc_y1 = y;

#if SFX_ENABLE_I2S
    /* ESP32-S3 実機スピーカー向け軽量 2-Tap アーリーリフレクション (空間の奥行き付与) */
    float tap1 = er_buf[(er_idx - 317) & (ER_LEN - 1)]; /* ~14.4ms @ 22kHz */
    float tap2 = er_buf[(er_idx - 677) & (ER_LEN - 1)]; /* ~30.7ms @ 22kHz */
    er_buf[er_idx] = y + tap2 * 0.14f;
    er_idx = (er_idx + 1) & (ER_LEN - 1);
    y = y + tap1 * 0.16f + tap2 * 0.10f;
#endif

    return soft_sat(y);
}

void render_f32(float* dst, int frames)
{
    if (!dst || frames <= 0) return;
    if (s_muted || s_vol_pct == 0) {
        memset(dst, 0, (size_t)frames * sizeof(float));
        return;
    }

    float master = (float)s_vol_pct * 0.01f;
    SFX_LOCK();
    float sr = (float)s_rate;
    float inv_sr = 1.0f / sr;
    bool any = false;
    for (int i = 0; i < MAX_VOICES; ++i) if (voices[i].active) { any = true; break; }
    if (!any && fabsf(dc_y1) < 1e-5f) {
        SFX_UNLOCK();
        memset(dst, 0, (size_t)frames * sizeof(float));
        return;
    }
    for (int n = 0; n < frames; ++n) {
        float mix = 0.0f;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active) mix += synth_voice_sample(voices[i], inv_sr, sr);
        }
        dst[n] = process_master_sample(mix * master);
    }
    SFX_UNLOCK();
}

void render_i16(int16_t* dst, int frames)
{
    if (!dst || frames <= 0) return;
    if (s_muted || s_vol_pct == 0) {
        memset(dst, 0, (size_t)frames * sizeof(int16_t));
        return;
    }

    float master = (float)s_vol_pct * 0.01f;
    SFX_LOCK();
    float sr = (float)s_rate;
    float inv_sr = 1.0f / sr;
    bool any = false;
    for (int i = 0; i < MAX_VOICES; ++i) if (voices[i].active) { any = true; break; }
#if !SFX_ENABLE_I2S
    if (!any && fabsf(dc_y1) < 1e-5f) {
        SFX_UNLOCK();
        memset(dst, 0, (size_t)frames * sizeof(int16_t));
        return;
    }
#else
    (void)any;
#endif
    for (int n = 0; n < frames; ++n) {
        float mix = 0.0f;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active) mix += synth_voice_sample(voices[i], inv_sr, sr);
        }
        float s = process_master_sample(mix * master);
        int32_t q = (int32_t)(s * 29000.0f);
        if (q > 32767) q = 32767;
        if (q < -32768) q = -32768;
        dst[n] = (int16_t)q;
    }
    SFX_UNLOCK();
}

} // namespace sfx
