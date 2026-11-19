# HANDOVER — 引き継ぎドキュメント

最終更新: 2026-11-18
対象: 『ルミの水晶庭園』(ESP32-S3 スマートスクリーン用 / v4.0 50-Wave Roguelite Overhaul)

> 実機書き込み: CH340 USB シリアル (VID:PID `1A86:7523`) は現在 **COM6** として認識されている (`platformio.ini` 既定は `COM5` のため、`pio run -t upload --upload-port COM6` で指定。`tools/grab_shot.py` と `tools/capture_runtime.py` は CH340 自動検出対応)。
> ユーザー指示: 作業完了ごとに実機へ書き込み・動作確認・スクショ検証を行い、起動状態 (Run #1 / Wave 1/50 クリーン状態) で引き渡すこと。
> 並列アクセス厳禁: `COM6` を掴むコマンド (`pio upload` / `grab_shot.py` / `capture_runtime.py`) は必ず直列実行すること (並列実行すると `PermissionError(13)` が発生する)。

新しく開発を引き継ぐ人は、このドキュメント → 各詳細ドキュメント の順に読んでください。

---

## 1. プロジェクトの現状 (ひとことで)

**実機 (480×480 タッチ屏) 上で、全50ウェーブ完結型の高難易度ローグライト・オートバトラーが動作中。**
弱い **LUMI ★1 単独** (放置収入ゼロ) からスタートし、ルートボックス (`LOOTBOX: PICK 1`) の **チャーム (36種)** と **Scout New Talent (64キャラ: N26 / R20 / SR12 / SSR6)** だけでパーティを強化。床ハザード・MP奪取・迫撃砲・棘反射などの嫌がらせ攻撃と、**HP 50% / 30% / 10% で固有アクションを発動する 6 種のボス** を攻略して **Wave 50 クリアのエンディング** を目指す（モンテカルロ 10,000 ラン検証でクリア率 **0.88%〜1.20%**）。

- 全 64 キャラ動的パレットリマップ対応スプライト (`src/actor_sprites.inc` + `src/roguelite_data.h` + ベース 7 種 36 コマアトラス)
- 49 種セルアニメ戦闘 VFX (`src/generated/fx_combat_atlas.h` + `tools/build_vfx_atlas.py`) + 予告リング・床ハザード・6種ボス50/30/10%固有演出
- 3 層パララックス水晶庭園背景 & 5 種カットイン (`src/generated/scenery_assets.h` + `tools/build_scenery_and_ui.py`)
- 全50ウェーブ制・有限決着 (`DIR_GAME_OVER` で敗北→能力引き継ぎなしの Wave 1 再挑戦 / `DIR_ENDING` で Wave 50 エンディング)

## 2. ハードウェア

| 項目 | 値 |
|---|---|
| デバイス | Shenzhen Jingcai Intelligent **ESP32-4848S040C_I_Y_3** (4.0" 480×480) |
| SoC | ESP32-S3R8 (内蔵 8MB octal PSRAM) / Flash 16MB |
| LCD | ST7701 (3-wire SPI 初期化 + RGB565 16bit パラレル, 98Hz バウンスDMA) |
| タッチ | GT911 (I2C: SCL=45 / SDA=19) |
| USB シリアル | CH340 → **COM6** (`1A86:7523` 自動検出 / フォールバック `COM6`→`COM5`) |
| 詳細 | `docs/DEVICE_SPEC.md` (ピン配置・純正 FW 解析・バックアップ手順) |

## 3. リポジトリ構成

```
platformio.ini                ビルド設定 (pioarduino + arduino-esp32 3.1.1 + JingCai ボード定義)
src/
  main.cpp                    起動 / ゲームループ / タッチ処理 / シリアル調整コンソール
  beat.cpp / .h               オートバトルエンジン (8戦闘スタイル/12Trait/6必殺) & 15x11 32行SRAMバンド合成器
  actor_sprites.inc           全7アトラス 0..35 ポーズ + 動的RLE色替え(64キャラ/ボス対応) + カーソルBlitter
  roguelite_data.h            全64キャラ(N/R/SR/SSR)・36チャーム(31効果)・6ボスのFlash常駐マスターデータ
  garden_game.cpp / .h        ローグライト経済・3択ドラフト・ランクアップ/入替・メタ解放・NVS "garden/save4"
  game_params.cpp / .h        調整パラメータ (シリアルから実行中に書き換え可)
  generated/                  ビルドスクリプトが生成する RGB565 スパン配列群
    {lumi,mira,goro,slime,bat,golem,boss}_atlas.h
    fx_combat_atlas.h         49種戦闘VFXアトラス (剣閃/打撃スパーク/衝撃波/爆発/宝石/紙吹雪)
    scenery_assets.h          空512x96 + 地面512x256 + 中景プロップ3種 + カットイン5種
tools/
  build_atlases.py            全7キャラ 6x6 アトラス PNG → RLE スパン C 配列変換
  build_vfx_atlas.py          4x スーパーサンプリング手描きセルVFX生成 → fx_combat_atlas.h
  build_scenery_and_ui.py     3層パララックス背景 & カットイン変換 → scenery_assets.h
  sim_roguelite_balance.py    roguelite_data.h 直読のモンテカルロ 10,000ラン バランス検証器
  grab_shot.py                実機 RLE スクリーンショット取得 (CH340 自動検出)
  capture_runtime.py          実機テレメトリ連続記録 (CH340 自動検出)
docs/
  DEVICE_SPEC.md              デバイス解析 (確定スペック・ピン配置)
  GAME_DESIGN.md              ゲームデザイン v4.0 (50-Wave ローグライト仕様 & バランス根拠)
  ART_GUIDE.md                キャラ画像の生成指示書
  ANIMATION_ATLASES.md        スプライト・VFX・実機計測記録
  HANDOVER.md                 このファイル
  evidence/                   シミュ結果 (`roguelite-balance-sim.txt`), 実機ログ (`roguelite-runtime.txt`), スクショ (`shot_rl_*.png` 等)
backup/                       純正ファームウェアの完全バックアップ (git 対象外)
assets_src/                   生成済み原画 (キャラアトラス/背景/カットイン) & `previews/`
external/sakurai-skill/       桜井政博のゲーム制作原則スキル (参照用, git 対象外)
```

**ビルドから除外済み**: LVGL 関連 (lvgl_v8_port, lv_conf, lvgl 依存) は v3.1 で撤去。
ESP32_Display_Panel は LCD/タッチの初期化のみに使用 (GUI には使っていない)。

## 4. 開発環境と基本操作

| 操作 | コマンド |
|---|---|
| ビルド | `pio run` |
| 書き込み | `pio run -t upload --upload-port COM6` |
| バランス検証 (PC) | `python tools/sim_roguelite_balance.py` |
| スクリーンショット | `python tools/grab_shot.py --out docs/evidence/shot.png` |
| テレメトリ記録 | `python tools/capture_runtime.py --seconds 15 --command "hardreset" --out docs/evidence/roguelite-runtime.txt` |
| VFX / 背景再生成 | `python tools/build_vfx_atlas.py` / `python tools/build_scenery_and_ui.py` |

- `platformio.ini` の既定ポートは `COM5` だが実機環境では `COM6` (`--upload-port COM6` を指定)
- **921600 baud は CH340 でデータ破損するため使わないこと** (460800 baud を採用)
- Python の `pyserial` から接続する際はリセット抑止のため `s.dtr=False; s.rts=False` を `s.open()` 前に設定すること

### シリアルデバッグコマンド一覧 (v4.0)

| コマンド | 効果 |
|---|---|
| `?` / `help` | 調整パラメータ一覧 + 全デバッグコマンド表示 |
| `dmg_pct 150` / `enemy_hp_pct 90` など | パラメータを実行中に変更 (`include/game_params.h`, #046) |
| `give <v>` | クリスタル付与 (デバッグ) |
| `box` / `pick <0..2> [slot]` | ルートボックス強制開封 / ドラフトカード選択 (満員時は `slot` 指定) |
| `scout <0..63>` / `charm <0..35>` | 指定キャラ雇用・ランクアップ / 指定チャーム付与 |
| `wave <1..50>` / `boss` / `bphase <50\|30\|10>` | 指定ウェーブへ移動 / ボス出現 / ボス 50/30/10% アクション強制発動 |
| `ult` / `win` / `heal` | 必殺技即発動 / 即時勝利 / 味方全回復 |
| `gameover` / `ending` | 全滅画面 (`DIR_GAME_OVER`) / Wave50エンディング (`DIR_ENDING`) 強制発動 |
| `unlockall` | 排出プール64人+36チャーム＆図鑑をテスト解放 |
| `reset` / `hardreset` | 現ランリスタート / メタ情報含む完全初期化 (Run #1, Wave 1, Solo LUMI) |
| `save` / `dump` / `shot` | NVS 保存 / 状態ダンプ / 480×480 RLE スクショ出力 |

## 5. アーキテクチャ

### 描画パイプライン (ダブルバッファ + キャッシュ同期)

```
毎フレーム (loop() の frame()):
  1. poll_touch()           GT911 を直接ポーリング (タップ/縦スワイプ検出)
  2. game::tick(dt)         なでなでタイマー進行のみ (放置収入はゼロ)
  3. beat::step()           戦闘シミュレーション (敵嫌がらせAI・ハザード・ボス50/30/10%判定)
  4. beat::render(target)   ゲーム絵を裏バッファの行 56..407 に描画
     beat::render_hud(target)      HUD (HP/通貨/ウェーブ/毎フレームミニマップ/BOXボタン)
     beat::render_panel(target)    3択ドラフト / スロット入替 / ステータス+チャーム一覧
  5. esp_cache_msync(canvas, C2M)   ★必須★
  6. switchFrameBufferTo(target)    スキャン対象を裏バッファへ (ポインタ切替のみ)
```

- **`esp_cache_msync(... C2M)` と `fb_done_sem` 同期は絶対に変更しないこと** (GDMA はキャッシュを迂回して PSRAM を読むため、省略すると画面更新停止・ティアリングが起きる)。
- **ゼロ Flash 肥大化の 64 キャラ色替え (`src/actor_sprites.inc`)**: ベース 3 アトラス (`lumi` / `mira` / `goro`) と敵アトラスのスパンを `actor_pose_buf` へコピーし、`remap_span_palette()` が黒系輪郭 (`mx < 52`)・白ハイライト (`mn > 214 && sat < 34`)・暖色肌を保護したまま服/髪/武器を書き換えるため、`blit_span_band_cursor()` の高速 `memcpy` パスを一切損なわない。ドラフトカード上のキャラプレビュー (`ensure_preview_pose`) は一時的に `actor_pose_buf[ALLY_N + 0..2]` を借用して描画後にキャッシュ無効化することで追加 RAM 消費ゼロを実現。
- **v3.x からの不変ルール**:
  - 全体のヒットストップ (時間停止) は禁止。被弾側微振動・ノックバック・明暗フラッシュ・スクワッシュで手応えを出す。
  - ダメージ/回復/ドロップ数字は最前面 (`layer = 2`) に黒アウトライン付きで描画。
  - ミニマップ (`y=408..424`) は毎フレーム描画 (`map_dirty = true`)。

### 経済・セーブ (`garden_game.cpp`)

- NVS `"garden"` 名前空間・キー `"save4"` (`SavePOD`, `magic = 0x4C554D49u`, `ver = 4`) を使用。
- ラン中は通貨・経過 Wave・パーティ 3 枠 (`talent_id`, `rank`)・所持チャーム (`charm_lv[36]`)・ドラフト状態を保存。
- 敗北/クリア時は `best_wave` / `run_number` / `total_clears` から `unlock_tier (0..8)` を更新してプール (`unlocked_talents`, `unlocked_charms`) と図鑑 (`discovered_talents`) のみ永続化し、能力値は一切引き継がない。

## 6. 実測データ・エビデンス (2026-11-18 COM6 検証済み)

- **ビルドサイズ (`pio run -t upload --upload-port COM6`)**:
  - Static RAM: **`9.8%` (`32,200 / 327,680` B)**
  - Flash: **`54.1%` (`3,548,774 / 6,553,600` B)**
- **実機ランタイムテレメトリ (`docs/evidence/roguelite-runtime.txt`)**:
  - Run #1 Solo LUMI Wave 1→3: **`fps = 49`**, `render = 12,971–14,935 us`, `int_max = 104,436 B`, `psram_free = 3,107,592 B`, `cpu = 240 MHz`
- **モンテカルロ バランス検証 (`docs/evidence/roguelite-balance-sim.txt`)**:
  - 新規セーブ 5,000 ラン (`50×100`): **クリア率 `0.88%`** (`avgW = 20.04`)
  - 全解禁 5,000 ラン: **クリア率 `1.20%`** (`avgW = 20.33`)
- **実機スクリーンショット (`docs/evidence/shot_rl_*.png`)**:
  - `shot_rl_wave1_solo.png` — 初期ソロ LUMI ★1 戦闘・空スロット・Wave 1/50・毎フレームミニマップ・`layer=2` 数字
  - `shot_rl_lootbox_draft.png` — 3択ドラフトカード (`Scout New Talent` 色替えプレビュー + `Charm`)
  - `shot_rl_harass_party.png` — 3人パーティ + 床ハザード・敵弾・状態異常バッジ・ミニマップハザード表示
  - `shot_rl_boss_phase.png` — 巨神ボス `IRONCLAD COLOSSUS` 50% Phase `MAGMA MORTAR!` 発動瞬間
  - `shot_rl_gameover.png` — `DEFEATED...` 全滅リザルト
  - `shot_rl_ending.png` — Wave 50 `CRYSTAL GARDEN SAVED!!` 完全浄化エンディング
